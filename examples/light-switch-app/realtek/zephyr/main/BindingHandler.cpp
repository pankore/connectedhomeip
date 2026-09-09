/*
 *
 *    Copyright (c) 2020-2024 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "BindingHandler.h"

#include "AppConfig.h"
#include "app/CommandSender.h"
#include "app/clusters/bindings/BindingManager.h"
#include "app/server/Server.h"
#include "controller/InvokeInteraction.h"
#include "lib/support/CodeUtils.h"
#include "platform/CHIPDeviceLayer.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

void ProcessOnOffUnicastBindingCommand(CommandId commandId, const Binding::TableEntry & binding,
                                       BindingHandler::BindingData & data, Messaging::ExchangeManager * exchangeMgr,
                                       const SessionHandle & sessionHandle)
{
    auto onSuccess = [](const ConcreteCommandPath & commandPath, const StatusIB & status, const auto & dataResponse) {
        ChipLogProgress(NotSpecified, "OnOff command applied successfully");
    };

    auto onFailure = [dataRef = data](CHIP_ERROR error) mutable {
        BindingHandler::OnInvokeCommandFailure(dataRef, error);
    };

    switch (commandId)
    {
    case Clusters::OnOff::Commands::Toggle::Id:
        Clusters::OnOff::Commands::Toggle::Type toggleCommand;
        LogErrorOnFailure(
            Controller::InvokeCommandRequest(exchangeMgr, sessionHandle, binding.remote, toggleCommand, onSuccess, onFailure));
        break;

    case Clusters::OnOff::Commands::On::Id:
        Clusters::OnOff::Commands::On::Type onCommand;
        LogErrorOnFailure(
            Controller::InvokeCommandRequest(exchangeMgr, sessionHandle, binding.remote, onCommand, onSuccess, onFailure));
        break;

    case Clusters::OnOff::Commands::Off::Id:
        Clusters::OnOff::Commands::Off::Type offCommand;
        LogErrorOnFailure(
            Controller::InvokeCommandRequest(exchangeMgr, sessionHandle, binding.remote, offCommand, onSuccess, onFailure));
        break;

    default:
        ChipLogError(NotSpecified, "Invalid binding onoff command id: 0x%x", commandId);
        break;
    }
}

void ProcessOnOffGroupBindingCommand(CommandId commandId, const Binding::TableEntry & binding)
{
    Messaging::ExchangeManager & exchangeMgr = Server::GetInstance().GetExchangeManager();

    switch (commandId)
    {
    case Clusters::OnOff::Commands::Toggle::Id:
        Clusters::OnOff::Commands::Toggle::Type toggleCommand;
        LogErrorOnFailure(Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, toggleCommand));
        break;

    case Clusters::OnOff::Commands::On::Id:
        Clusters::OnOff::Commands::On::Type onCommand;
        LogErrorOnFailure(Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, onCommand));
        break;

    case Clusters::OnOff::Commands::Off::Id:
        Clusters::OnOff::Commands::Off::Type offCommand;
        LogErrorOnFailure(Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, offCommand));
        break;

    default:
        ChipLogError(NotSpecified, "Invalid binding onoff group command id: 0x%x", commandId);
        break;
    }
}

} // namespace

void BindingHandler::Init()
{
    // The initialization of binding manager will try establishing connection with unicast peers
    // so it requires the Server instance to be correctly initialized. Post the init function to
    // the event queue so that everything is ready when initialization is conducted.
    LogErrorOnFailure(DeviceLayer::PlatformMgr().ScheduleWork(InitInternal));
}

void BindingHandler::InitInternal(intptr_t arg)
{
    auto & server = chip::Server::GetInstance();
    CHIP_ERROR err =
        Binding::Manager::GetInstance().Init({ &server.GetFabricTable(), server.GetCASESessionManager(), &server.GetPersistentStorage() });
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Binding::Manager::Init() failed: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    Binding::Manager::GetInstance().RegisterBoundDeviceChangedHandler(LightSwitchChangedHandler);
    Binding::Manager::GetInstance().RegisterBoundDeviceContextReleaseHandler(LightSwitchContextReleaseHandler);
}

void BindingHandler::OnInvokeCommandFailure(BindingData & aBindingData, CHIP_ERROR aError)
{
    if (aError == CHIP_ERROR_TIMEOUT && !BindingHandler::GetInstance().mCaseSessionRecovered)
    {
        ChipLogProgress(NotSpecified, "Response timeout for invoked command, trying to recover CASE session.");

        // Set flag to not try recover session multiple times.
        BindingHandler::GetInstance().mCaseSessionRecovered = true;

        // Allocate new object to make sure its life time will be appropriate.
        BindingData * data = Platform::New<BindingData>();
        *data              = aBindingData;

        // Establish new CASE session and retransmit command that was not applied.
        CHIP_ERROR error = Binding::Manager::GetInstance().NotifyBoundClusterChanged(aBindingData.EndpointId, aBindingData.ClusterId,
                                                                                     static_cast<void *>(data));
        if (CHIP_NO_ERROR != error)
        {
            ChipLogError(NotSpecified, "NotifyBoundClusterChanged failed due to: %" CHIP_ERROR_FORMAT, error.Format());
            return;
        }
    }
    else
    {
        ChipLogError(NotSpecified, "Binding command was not applied! Reason: %" CHIP_ERROR_FORMAT, aError.Format());
    }
}

void BindingHandler::LightSwitchContextReleaseHandler(void * context)
{
    VerifyOrReturn(context != nullptr, ChipLogError(NotSpecified, "LightSwitchContextReleaseHandler: context is null"));

    Platform::Delete(static_cast<BindingData *>(context));
}

void BindingHandler::OnOffProcessCommand(CommandId commandId, const Binding::TableEntry & binding,
                                         OperationalDeviceProxy * deviceProxy, void * context)
{
    BindingData * data = reinterpret_cast<BindingData *>(context);

    if (binding.type == Binding::MATTER_MULTICAST_BINDING && data->IsGroup)
    {
        ProcessOnOffGroupBindingCommand(commandId, binding);
    }
    else if (binding.type == Binding::MATTER_UNICAST_BINDING && !data->IsGroup)
    {
        VerifyOrDie(deviceProxy != nullptr && deviceProxy->ConnectionReady());
        ProcessOnOffUnicastBindingCommand(commandId, binding, *data, deviceProxy->GetExchangeManager(),
                                          deviceProxy->GetSecureSession().Value());
    }
}

void BindingHandler::LevelControlProcessCommand(CommandId commandId, const Binding::TableEntry & binding,
                                                OperationalDeviceProxy * deviceProxy, void * context)
{
    BindingData * data = reinterpret_cast<BindingData *>(context);

    auto onSuccess = [](const ConcreteCommandPath & commandPath, const StatusIB & status, const auto & dataResponse) {
        ChipLogProgress(NotSpecified, "LevelControl command applied successfully");
    };

    auto onFailure = [dataRef = *data](CHIP_ERROR error) mutable {
        BindingHandler::OnInvokeCommandFailure(dataRef, error);
    };

    if (commandId != Clusters::LevelControl::Commands::MoveToLevel::Id)
    {
        ChipLogError(NotSpecified, "Invalid binding LevelControl command id: 0x%x", commandId);
        return;
    }

    Clusters::LevelControl::Commands::MoveToLevel::Type moveToLevelCommand;
    moveToLevelCommand.level = data->Value;

    if (binding.type == Binding::MATTER_MULTICAST_BINDING && data->IsGroup)
    {
        Messaging::ExchangeManager & exchangeMgr = Server::GetInstance().GetExchangeManager();
        LogErrorOnFailure(Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId,
                                                                moveToLevelCommand));
    }
    else if (binding.type == Binding::MATTER_UNICAST_BINDING && !data->IsGroup)
    {
        VerifyOrDie(deviceProxy != nullptr && deviceProxy->ConnectionReady());
        LogErrorOnFailure(Controller::InvokeCommandRequest(deviceProxy->GetExchangeManager(), deviceProxy->GetSecureSession().Value(),
                                                           binding.remote, moveToLevelCommand, onSuccess, onFailure));
    }
}

void BindingHandler::LightSwitchChangedHandler(const Binding::TableEntry & binding, OperationalDeviceProxy * deviceProxy,
                                               void * context)
{
    VerifyOrReturn(context != nullptr, ChipLogError(NotSpecified, "OnDeviceConnectedFn: context is null"));
    BindingData * data = static_cast<BindingData *>(context);

    switch (data->ClusterId)
    {
    case Clusters::OnOff::Id:
        OnOffProcessCommand(data->CommandId, binding, deviceProxy, context);
        break;
    case Clusters::LevelControl::Id:
        LevelControlProcessCommand(data->CommandId, binding, deviceProxy, context);
        break;
    default:
        ChipLogError(NotSpecified, "Invalid binding cluster id: 0x%x", data->ClusterId);
        break;
    }
}

void BindingHandler::SwitchWorkerFunction(intptr_t context)
{
    VerifyOrReturn(context != 0, ChipLogError(NotSpecified, "SwitchWorkerFunction - Invalid work data"));

    BindingData * data = reinterpret_cast<BindingData *>(context);
    LogErrorOnFailure(
        Binding::Manager::GetInstance().NotifyBoundClusterChanged(data->EndpointId, data->ClusterId, static_cast<void *>(data)));
}
