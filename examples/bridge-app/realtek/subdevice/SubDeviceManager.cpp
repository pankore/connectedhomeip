/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
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

#include "SubDeviceManager.h"
#include "SubDevice.h"
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/InteractionModelEngine.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/reporting/reporting.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <assert.h>
#include <lib/core/CHIPError.h>
#include <lib/core/ErrorStr.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/ZclString.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;
using namespace ::chip::System;
using namespace ::chip::Platform;
using namespace ::chip::app::Clusters;

// Cluster revision definitions (from zap generated or hardcoded)
#define ZCL_ON_OFF_CLUSTER_REVISION (4u)
#define ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION (1u)
#define ZCL_THERMOSTAT_CLUSTER_REVISION (9u)
#define ZCL_FAN_CONTROL_CLUSTER_REVISION (5u)
#define ZCL_THERMOSTAT_UI_CLUSTER_REVISION (1u)
#define DEVICE_TYPE_ROOT_NODE (0x0016)
#define DEVICE_TYPE_BRIDGE (0x000e)
#define DEVICE_TYPE_LO_ON_OFF_LIGHT (0x0100)
#define DEVICE_TYPE_BRIDGED_NODE (0x0013)
#define DEVICE_VERSION_DEFAULT (0)

static EndpointId gCurrentEndpointId;
static EndpointId gFirstDynamicEndpointId;

static SubDevice * gSubDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT]; // number of dynamic endpoints count

int AddDeviceEndpoint(SubDevice * dev, EmberAfEndpointType * ep, const Span<const EmberAfDeviceType> & deviceTypeList,
                      const Span<DataVersion> & dataVersionStorage, chip::EndpointId parentEndpointId)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (NULL == gSubDevices[index])
        {
            gSubDevices[index] = dev;
            CHIP_ERROR err;
            while (1)
            {
                dev->SetEndpointId(gCurrentEndpointId);
                err =
                    emberAfSetDynamicEndpoint(index, gCurrentEndpointId, ep, dataVersionStorage, deviceTypeList, parentEndpointId);
                if (err == CHIP_NO_ERROR)
                {
                    ChipLogProgress(DeviceLayer, "Added device %s to dynamic endpoint %d (index=%d)", dev->GetName(),
                                    gCurrentEndpointId, index);
                    return index;
                }
                else if (err != CHIP_ERROR_ENDPOINT_EXISTS)
                {
                    return -1;
                }
                // Handle wrap condition
                if (++gCurrentEndpointId < gFirstDynamicEndpointId)
                {
                    gCurrentEndpointId = gFirstDynamicEndpointId;
                }
            }
        }
        index++;
    }
    ChipLogProgress(DeviceLayer, "Failed to add dynamic endpoint: No endpoints available!");
    return -1;
}

CHIP_ERROR RemoveDeviceEndpoint(SubDevice * dev)
{
    for (uint8_t index = 0; index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; index++)
    {
        if (gSubDevices[index] == dev)
        {
            // Silence complaints about unused ep when progress logging
            // disabled.
            [[maybe_unused]] EndpointId ep = emberAfClearDynamicEndpoint(index);
            gSubDevices[index]             = NULL;
            ChipLogProgress(DeviceLayer, "Removed device %s from dynamic endpoint %d (index=%d)", dev->GetName(), ep, index);
            return CHIP_NO_ERROR;
        }
    }
    return CHIP_ERROR_INTERNAL;
}

Protocols::InteractionModel::Status HandleReadBridgedDeviceBasicAttribute(SubDevice * dev, chip::AttributeId attributeId,
                                                                          uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace BridgedDeviceBasicInformation::Attributes;
    ChipLogProgress(DeviceLayer, "HandleReadBridgedDeviceBasicAttribute: attrId=%" PRIu32 ", maxReadLength=%u", attributeId,
                    maxReadLength);

    if ((attributeId == Reachable::Id) && (maxReadLength == 1))
    {
        *buffer = dev->IsReachable() ? 1 : 0;
    }
    else if ((attributeId == NodeLabel::Id) && (maxReadLength == 32))
    {
        MutableByteSpan zclNameSpan(buffer, maxReadLength);
        MakeZclCharString(zclNameSpan, dev->GetName());
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        *buffer = (uint16_t) ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION;
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadOnOffAttribute(SubDevice * dev, chip::AttributeId attributeId, uint8_t * buffer,
                                                             uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadOnOffAttribute: attrId=%" PRIu32 ", maxReadLength=%u", attributeId, maxReadLength);

    if ((attributeId == OnOff::Attributes::OnOff::Id) && (maxReadLength == 1))
    {
        *buffer = dev->IsOn() ? 1 : 0;
    }
    else if ((attributeId == OnOff::Attributes::ClusterRevision::Id) && (maxReadLength == 2))
    {
        *buffer = (uint16_t) ZCL_ON_OFF_CLUSTER_REVISION;
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteOnOffAttribute(SubDevice * dev, chip::AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteOnOffAttribute: attrId=%" PRIu32, attributeId);

    VerifyOrReturnError((attributeId == OnOff::Attributes::OnOff::Id) && dev->IsReachable(),
                        Protocols::InteractionModel::Status::Failure);
    dev->SetOnOff(*buffer == 1);
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadThermostatAttribute(SubDevice * dev, chip::AttributeId attributeId,
                                                                  uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace Thermostat::Attributes;
    ChipLogProgress(DeviceLayer, "HandleReadThermostatAttribute: attrId=%" PRIu32 ", maxReadLength=%u", attributeId, maxReadLength);

    // Check if device is ThermostatDevice
    if (dev->GetDeviceType() != SubDevice::kDeviceType_Thermostat)
    {
        ChipLogError(DeviceLayer, "Device is not a ThermostatDevice");
        return Protocols::InteractionModel::Status::Failure;
    }

    // Cast to ThermostatDevice
    ThermostatDevice * thermostat = static_cast<ThermostatDevice *>(dev);

    // Return stored values for Thermostat attributes
    if ((attributeId == LocalTemperature::Id) && (maxReadLength == 2))
    {
        // Default: 20°C (2000 in 0.01°C units)
        *buffer++ = 0xD0;
        *buffer = 0x07;
    }
    else if ((attributeId == OccupiedCoolingSetpoint::Id) && (maxReadLength == 2))
    {
        // Return stored value
        int16_t temp = thermostat->GetOccupiedCoolingSetpoint();
        *buffer++ = (uint8_t)(temp & 0xFF);
        *buffer = (uint8_t)((temp >> 8) & 0xFF);
    }
    else if ((attributeId == OccupiedHeatingSetpoint::Id) && (maxReadLength == 2))
    {
        // Return stored value
        int16_t temp = thermostat->GetOccupiedHeatingSetpoint();
        *buffer++ = (uint8_t)(temp & 0xFF);
        *buffer = (uint8_t)((temp >> 8) & 0xFF);
    }
    else if ((attributeId == SystemMode::Id) && (maxReadLength == 1))
    {
        // Return stored value
        *buffer = thermostat->GetSystemMode();
    }
    else if ((attributeId == FeatureMap::Id) && (maxReadLength == 4))
    {
        // FeatureMap: 0x1A3
        *buffer++ = 0xA3;  // low byte
        *buffer++ = 0x01;  // high byte
        *buffer++ = 0x00;
        *buffer = 0x00;
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        *buffer = (uint16_t) ZCL_THERMOSTAT_CLUSTER_REVISION;
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteThermostatAttribute(SubDevice * dev, chip::AttributeId attributeId,
                                                                    uint8_t * buffer)
{
    using namespace Thermostat::Attributes;
    ChipLogProgress(DeviceLayer, "HandleWriteThermostatAttribute: attrId=%" PRIu32, attributeId);

    VerifyOrReturnError(dev->IsReachable(), Protocols::InteractionModel::Status::Failure);

    // Check if device is ThermostatDevice
    if (dev->GetDeviceType() != SubDevice::kDeviceType_Thermostat)
    {
        ChipLogError(DeviceLayer, "Device is not a ThermostatDevice");
        return Protocols::InteractionModel::Status::Failure;
    }

    // Cast to ThermostatDevice
    ThermostatDevice * thermostat = static_cast<ThermostatDevice *>(dev);

    // Handle writable Thermostat attributes
    if (attributeId == OccupiedCoolingSetpoint::Id)
    {
        int16_t temp = (int16_t)(*buffer | (*(buffer + 1) << 8));
        thermostat->SetOccupiedCoolingSetpoint(temp);
        ChipLogProgress(DeviceLayer, "Set OccupiedCoolingSetpoint: %d", temp);
    }
    else if (attributeId == OccupiedHeatingSetpoint::Id)
    {
        int16_t temp = (int16_t)(*buffer | (*(buffer + 1) << 8));
        thermostat->SetOccupiedHeatingSetpoint(temp);
        ChipLogProgress(DeviceLayer, "Set OccupiedHeatingSetpoint: %d", temp);
    }
    else if (attributeId == SystemMode::Id)
    {
        thermostat->SetSystemMode(*buffer);
        ChipLogProgress(DeviceLayer, "Set SystemMode: %d", *buffer);
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadFanControlAttribute(SubDevice * dev, chip::AttributeId attributeId,
                                                                  uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace FanControl::Attributes;
    ChipLogProgress(DeviceLayer, "HandleReadFanControlAttribute: attrId=%" PRIu32 ", maxReadLength=%u", attributeId, maxReadLength);

    // Return default values for FanControl attributes
    if ((attributeId == FanMode::Id) && (maxReadLength == 1))
    {
        // Default: Auto (0)
        *buffer = 0;
    }
    else if ((attributeId == FanModeSequence::Id) && (maxReadLength == 1))
    {
        // Default: Off/Low/Medium/Auto (2)
        *buffer = 2;
    }
    else if ((attributeId == PercentSetting::Id) && (maxReadLength == 1))
    {
        // Default: 50%
        *buffer = 50;
    }
    else if ((attributeId == PercentCurrent::Id) && (maxReadLength == 1))
    {
        // Default: 50%
        *buffer = 50;
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        *buffer = (uint16_t) ZCL_FAN_CONTROL_CLUSTER_REVISION;
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteFanControlAttribute(SubDevice * dev, chip::AttributeId attributeId,
                                                                    uint8_t * buffer)
{
    using namespace FanControl::Attributes;
    ChipLogProgress(DeviceLayer, "HandleWriteFanControlAttribute: attrId=%" PRIu32, attributeId);

    VerifyOrReturnError(dev->IsReachable(), Protocols::InteractionModel::Status::Failure);

    // Handle writable FanControl attributes
    if (attributeId == FanMode::Id)
    {
        ChipLogProgress(DeviceLayer, "Set FanMode: %d", *buffer);
    }
    else if (attributeId == PercentSetting::Id)
    {
        ChipLogProgress(DeviceLayer, "Set PercentSetting: %d%%", *buffer);
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadThermostatUIAttribute(SubDevice * dev, chip::AttributeId attributeId,
                                                                     uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace ThermostatUserInterfaceConfiguration::Attributes;
    ChipLogProgress(DeviceLayer, "HandleReadThermostatUIAttribute: attrId=%" PRIu32 ", maxReadLength=%u", attributeId, maxReadLength);

    // Return default values for Thermostat UI attributes
    if ((attributeId == TemperatureDisplayMode::Id) && (maxReadLength == 1))
    {
        // Default: Celsius (0)
        *buffer = 0;
    }
    else if ((attributeId == KeypadLockout::Id) && (maxReadLength == 1))
    {
        // Default: No lockout (0)
        *buffer = 0;
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        *buffer = (uint16_t) ZCL_THERMOSTAT_UI_CLUSTER_REVISION;
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteThermostatUIAttribute(SubDevice * dev, chip::AttributeId attributeId,
                                                                     uint8_t * buffer)
{
    using namespace ThermostatUserInterfaceConfiguration::Attributes;
    ChipLogProgress(DeviceLayer, "HandleWriteThermostatUIAttribute: attrId=%" PRIu32, attributeId);

    VerifyOrReturnError(dev->IsReachable(), Protocols::InteractionModel::Status::Failure);

    // Handle writable Thermostat UI attributes
    if (attributeId == TemperatureDisplayMode::Id)
    {
        ChipLogProgress(DeviceLayer, "Set TemperatureDisplayMode: %d", *buffer);
    }
    else if (attributeId == KeypadLockout::Id)
    {
        ChipLogProgress(DeviceLayer, "Set KeypadLockout: %d", *buffer);
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                                         const EmberAfAttributeMetadata * attributeMetadata,
                                                                         uint8_t * buffer, uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (gSubDevices[endpointIndex] != NULL))
    {
        SubDevice * dev = gSubDevices[endpointIndex];

        // if (clusterId == BridgedDeviceBasic::Id)
        if (clusterId == BridgedDeviceBasicInformation::Id)
        {
            return HandleReadBridgedDeviceBasicAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == OnOff::Id)
        {
            return HandleReadOnOffAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == Thermostat::Id)
        {
            return HandleReadThermostatAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == FanControl::Id)
        {
            return HandleReadFanControlAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == ThermostatUserInterfaceConfiguration::Id)
        {
            return HandleReadThermostatUIAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
    }

    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                                          const EmberAfAttributeMetadata * attributeMetadata,
                                                                          uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    ChipLogProgress(DeviceLayer, "WriteCallback: ep=%d, index=%d, cluster=0x%" PRIx32 ", attr=0x%" PRIx32,
                    endpoint, endpointIndex, clusterId, attributeMetadata->attributeId);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        SubDevice * dev = gSubDevices[endpointIndex];

        if ((dev->IsReachable()) && (clusterId == OnOff::Id))
        {
            return HandleWriteOnOffAttribute(dev, attributeMetadata->attributeId, buffer);
        }
        else if ((dev->IsReachable()) && (clusterId == Thermostat::Id))
        {
            return HandleWriteThermostatAttribute(dev, attributeMetadata->attributeId, buffer);
        }
        else if ((dev->IsReachable()) && (clusterId == FanControl::Id))
        {
            return HandleWriteFanControlAttribute(dev, attributeMetadata->attributeId, buffer);
        }
        else if ((dev->IsReachable()) && (clusterId == ThermostatUserInterfaceConfiguration::Id))
        {
            return HandleWriteThermostatUIAttribute(dev, attributeMetadata->attributeId, buffer);
        }
    }

    return Protocols::InteractionModel::Status::Failure;
}

namespace {
void CallReportingCallback(intptr_t closure)
{
    auto path = reinterpret_cast<app::ConcreteAttributePath *>(closure);
    MatterReportingAttributeChangeCallback(*path);
    Platform::Delete(path);
}

void ScheduleReportingCallback(SubDevice * dev, ClusterId cluster, AttributeId attribute)
{
    auto * path = Platform::New<app::ConcreteAttributePath>(dev->GetEndpointId(), cluster, attribute);
    DeviceLayer::PlatformMgr().ScheduleWork(CallReportingCallback, reinterpret_cast<intptr_t>(path));
}
} // anonymous namespace

void HandleDeviceStatusChanged(SubDevice * dev, SubDevice::Changed_t itemChangedMask)
{
    if (itemChangedMask & SubDevice::kChanged_Reachable)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id);
    }

    if (itemChangedMask & SubDevice::kChanged_State)
    {
        ScheduleReportingCallback(dev, OnOff::Id, OnOff::Attributes::OnOff::Id);
    }

    if (itemChangedMask & SubDevice::kChanged_Name)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
}

const EmberAfDeviceType gRootDeviceTypes[]          = { { DEVICE_TYPE_ROOT_NODE, DEVICE_VERSION_DEFAULT } };
const EmberAfDeviceType gAggregateNodeDeviceTypes[] = { { DEVICE_TYPE_BRIDGE, DEVICE_VERSION_DEFAULT } };

void Init_Bridge_Endpoint()
{
    // bridge will have own database named gSubDevices.
    // Clear database
    memset(gSubDevices, 0, sizeof(gSubDevices));

    // Set starting endpoint id where dynamic endpoints will be assigned, which
    // will be the next consecutive endpoint id after the last fixed endpoint.
    gFirstDynamicEndpointId = static_cast<chip::EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
    gCurrentEndpointId = gFirstDynamicEndpointId;

    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generated the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

    // A bridge has root node device type on EP0 and aggregate node device type (bridge) at EP1
    emberAfSetDeviceTypeList(0, Span<const EmberAfDeviceType>(gRootDeviceTypes));
    emberAfSetDeviceTypeList(1, Span<const EmberAfDeviceType>(gAggregateNodeDeviceTypes));
}