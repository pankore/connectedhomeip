/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include "AppTask.h"
#include "Downlink.h"

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-id.h>

#define APP_DOWNLINKTASK_NAME "Downlink"
#define APP_UPLINKTASK_NAME "Uplink"
#define APP_EVENT_QUEUE_SIZE 10
#define APP_TASK_STACK_SIZE (2048)
#define BUTTON_PRESSED 1
#define APP_LIGHT_SWITCH 1

#ifdef CONFIG_PLATFORM_8721D
#define STATUS_LED_GPIO_NUM PB_5
#elif defined(CONFIG_PLATFORM_8710C)
#define STATUS_LED_GPIO_NUM         PA_23
#define RED_LED_GPIO_NUM            PA_18
#define GREEN_LED_GPIO_NUM          PA_19
#define BLUE_LED_GPIO_NUM           PA_20
#define COOL_WHITE_LED_GPIO_NUM     PA_4
#define WARM_WHITE_LED_GPIO_NUM     PA_17
#else
#define STATUS_LED_GPIO_NUM         NC
#define RED_LED_GPIO_NUM            NC 
#define GREEN_LED_GPIO_NUM          NC 
#define BLUE_LED_GPIO_NUM           NC 
#define COOL_WHITE_LED_GPIO_NUM     NC
#define WARM_WHITE_LED_GPIO_NUM     NC
#endif

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;
using namespace ::chip::System;

LEDWidget AppLED;
Downlink Downlink;

namespace {
constexpr EndpointId kLightEndpointId = 1;
QueueHandle_t UplinkEventQueue;
QueueHandle_t DownlinkEventQueue;
TaskHandle_t UplinkTaskHandle;
TaskHandle_t DownlinkTaskHandle;
} // namespace

AppTask AppTask::sAppTask;

uint32_t identifyTimerCount;
constexpr uint32_t kIdentifyTimerDelayMS     = 250;

CHIP_ERROR AppTask::Init()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    AppLED.Init(STATUS_LED_GPIO_NUM);
    bool LEDOnOffValue = 0;
    DataModel::Nullable<uint8_t> LEDCurrentLevelValue;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    EmberAfStatus onoffstatus = Clusters::OnOff::Attributes::OnOff::Get(kLightEndpointId, &LEDOnOffValue);
    if (onoffstatus != EMBER_ZCL_STATUS_SUCCESS)
    {
        ChipLogError(DeviceLayer, "Failed to read onoff value: %x", onoffstatus);
        return CHIP_ERROR_INTERNAL;
    }

    EmberAfStatus currentlevelstatus = Clusters::LevelControl::Attributes::CurrentLevel::Get(kLightEndpointId, LEDCurrentLevelValue);
    if (currentlevelstatus != EMBER_ZCL_STATUS_SUCCESS)
    {
        ChipLogError(DeviceLayer, "Failed to read currentlevel value: %x", currentlevelstatus);
        return CHIP_ERROR_INTERNAL;
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    // Set LED to onoff value
    AppLED.Set(LEDOnOffValue);
    // Set LED to currentlevel value
    AppLED.SetBrightness(LEDCurrentLevelValue.Value());

    Downlink.Init();
    Downlink.SetDownlinkCallback(DownlinkOnOffCallback);
    // don't need to set uplink callback, it is MatterPostAttributeChangeCallback 
    
    return err;
}

CHIP_ERROR AppTask::StartDownlinkTask()
{
    DownlinkEventQueue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(AppEvent));
    if (DownlinkEventQueue == NULL)
    {
        ChipLogError(DeviceLayer, "Failed to allocate downlink event queue");
        return CHIP_ERROR_NO_MEMORY;
    }

    // Start Downlink task.
    BaseType_t xReturned;
    xReturned = xTaskCreate(DownlinkTask, APP_DOWNLINKTASK_NAME, APP_TASK_STACK_SIZE, NULL, 1, &DownlinkTaskHandle);

    return (xReturned == pdPASS) ? CHIP_NO_ERROR : CHIP_ERROR_NO_MEMORY;
}

void AppTask::DownlinkTask(void * pvParameter)
{
    AppEvent event;

    ChipLogProgress(DeviceLayer, "Downlink Task started");

    // Loop here and keep listening on the queue for Downlink (Firmware application to matter)
    while (true)
    {
        BaseType_t eventReceived = xQueueReceive(DownlinkEventQueue, &event, pdMS_TO_TICKS(10));
        while (eventReceived == pdTRUE)
        {
            sAppTask.DispatchDownlinkEvent(&event);
            eventReceived = xQueueReceive(DownlinkEventQueue, &event, 0); // return immediately if the queue is empty
        }
    }
}

void AppTask::PostDownlinkEvent(const AppEvent * aEvent)
{
    if (DownlinkEventQueue != NULL)
    {
        BaseType_t status;

        // Event is posted in ISR, use ISR api
        BaseType_t higherPrioTaskWoken = pdFALSE;
        status                         = xQueueSendFromISR(DownlinkEventQueue, aEvent, &higherPrioTaskWoken);

        if (!status)
            ChipLogError(DeviceLayer, "Failed to post downlink event to downlink event queue with");
    }
    else
    {
        ChipLogError(DeviceLayer, "Downlink Event Queue is NULL should never happen");
    }
}

void AppTask::DispatchDownlinkEvent(AppEvent * aEvent)
{
    if (aEvent->mHandler)
    {
        aEvent->mHandler(aEvent);
    }
    else
    {
        ChipLogError(DeviceLayer, "Downlink event received with no handler. Dropping event.");
    }
}

void AppTask::DownlinkOnOffEventHandler(AppEvent * aEvent)
{
    if (aEvent->Type != AppEvent::kEventType_Downlink_OnOff)
    {
        ChipLogError(DeviceLayer, "Wrong downlink event handler, should not happen!");
        return;
    }

    AppLED.Toggle();
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    sAppTask.UpdateClusterState(aEvent);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

// this callback is for on off attribute only
// create more callbacks for other attributes
void AppTask::DownlinkOnOffCallback()
{
    AppEvent downlink_event;
    downlink_event.Type     = AppEvent::kEventType_Downlink_OnOff;
    downlink_event.mHandler = AppTask::DownlinkOnOffEventHandler;
    sAppTask.PostDownlinkEvent(&downlink_event);
}

void AppTask::UpdateClusterState(AppEvent * event)
{
    switch (event->Type)
    {
        case AppEvent::kEventType_Downlink_OnOff:
            ChipLogProgress(DeviceLayer, "Writing to OnOff cluster");
            // write the new on/off value
            EmberAfStatus status = Clusters::OnOff::Attributes::OnOff::Set(kLightEndpointId, AppLED.IsTurnedOn());

            if (status != EMBER_ZCL_STATUS_SUCCESS)
            {
                ChipLogError(DeviceLayer, "Updating on/off cluster failed: %x", status);
            }

            ChipLogError(DeviceLayer, "Writing to Current Level cluster");
            // write the new currentlevel value
            status = Clusters::LevelControl::Attributes::CurrentLevel::Set(kLightEndpointId, AppLED.GetLevel());

            if (status != EMBER_ZCL_STATUS_SUCCESS)
            {
                ChipLogError(DeviceLayer, "Updating level cluster failed: %x", status);
            }
            break;

    // TODO: Add more attribute changes
    }
}

CHIP_ERROR AppTask::StartUplinkTask()
{
    UplinkEventQueue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(AppEvent));
    if (UplinkEventQueue == NULL)
    {
        ChipLogError(DeviceLayer, "Failed to allocate uplink event queue");
        return CHIP_ERROR_NO_MEMORY;
    }

    // Start Downlink task.
    BaseType_t xReturned;
    xReturned = xTaskCreate(UplinkTask, APP_UPLINKTASK_NAME, APP_TASK_STACK_SIZE, NULL, 1, &UplinkTaskHandle);
    return (xReturned == pdPASS) ? CHIP_NO_ERROR : CHIP_ERROR_NO_MEMORY;
}

void AppTask::UplinkTask(void * pvParameter)
{
    AppEvent event;

    ChipLogProgress(DeviceLayer, "Uplink Task started");

    // Loop here and keep listening on the queue for Uplink (matter to Firmware application)
    while (true)
    {
        BaseType_t eventReceived = xQueueReceive(UplinkEventQueue, &event, pdMS_TO_TICKS(10));
        while (eventReceived == pdTRUE)
        {
            sAppTask.DispatchUplinkEvent(&event);
            eventReceived = xQueueReceive(UplinkEventQueue, &event, 0); // return immediately if the queue is empty
        }
    }
}

void AppTask::PostUplinkEvent(const AppEvent * aEvent)
{
    if (UplinkEventQueue != NULL)
    {
        BaseType_t status;
        status = xQueueSend(UplinkEventQueue, aEvent, 1);

        if (!status)
            ChipLogError(DeviceLayer, "Failed to post uplink event to uplink event queue");
    }
    else
    {
        ChipLogError(DeviceLayer, "Uplink Event Queue is NULL should never happen");
    }
}

void AppTask::DispatchUplinkEvent(AppEvent * aEvent)
{
    if (aEvent->mHandler)
    {
        aEvent->mHandler(aEvent);
    }
    else
    {
        ChipLogError(DeviceLayer, "Uplink event received with no handler. Dropping event.");
    }
}

void AppTask::UplinkOnOffEventHandler(AppEvent * aEvent)
{
    VerifyOrExit(aEvent->path.mEndpointId == 1 || aEvent->path.mEndpointId == 2,
                 ChipLogError(DeviceLayer, "Unexpected EndPoint ID: `0x%02x'", aEvent->path.mEndpointId));
    switch (aEvent->path.mAttributeId)
    {
    case ZCL_ON_OFF_ATTRIBUTE_ID:
        AppLED.Set(aEvent->value);
        break;
    default:
        break;
    }

    // No need to update cluster state
exit:
    return;
}

void AppTask::UplinkLevelControlEventHandler(AppEvent * aEvent)
{
    VerifyOrExit(aEvent->path.mEndpointId == 1 || aEvent->path.mEndpointId == 2,
                 ChipLogError(DeviceLayer, "Unexpected EndPoint ID: `0x%02x'", aEvent->path.mEndpointId));
    switch (aEvent->path.mAttributeId)
    {
    case ZCL_CURRENT_LEVEL_ATTRIBUTE_ID:
        AppLED.SetBrightness(aEvent->value);
        break;
    default:
        break;
    }

    // No need to update cluster state
exit:
    return;
}

void IdentifyTimerHandler(Layer * systemLayer, void * appState, CHIP_ERROR error)
{
    if (identifyTimerCount)
    {
        // systemLayer->StartTimer(Clock::Milliseconds32(kIdentifyTimerDelayMS), IdentifyTimerHandler, appState);
        // Decrement the timer count.
        identifyTimerCount--;
    }
}

void AppTask::UplinkIdentifyEventHandler(AppEvent * aEvent)
{
    VerifyOrExit(aEvent->path.mAttributeId == ZCL_IDENTIFY_TIME_ATTRIBUTE_ID,
                 ChipLogError(DeviceLayer, "Unhandled Attribute ID: '0x%04x", aEvent->path.mAttributeId));
    VerifyOrExit(aEvent->path.mEndpointId == 1, ChipLogError(DeviceLayer, "Unexpected EndPoint ID: `0x%02x'", aEvent->path.mEndpointId));

    switch (aEvent->path.mAttributeId)
    {
    case ZCL_IDENTIFY_TIME_ATTRIBUTE_ID:
        // timerCount represents the number of callback executions before we stop the timer.
        // value is expressed in seconds and the timer is fired every 250ms, so just multiply value by 4.
        // Also, we want timerCount to be odd number, so the ligth state ends in the same state it starts.
        identifyTimerCount = (aEvent->value) * 4;
        break;
    }

exit:
    return;
}
// This is the uplink callback
void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & path, uint8_t type, uint16_t size, uint8_t * value)
{
    AppEvent uplink_event;
    uplink_event.Type = AppEvent::kEventType_Uplink;
    uplink_event.value = *value;
    uplink_event.path = path;

    switch (path.mClusterId)
    {
    case ZCL_ON_OFF_CLUSTER_ID:
        uplink_event.mHandler = AppTask::UplinkOnOffEventHandler;
        GetAppTask().PostUplinkEvent(&uplink_event);
        break;

    case ZCL_LEVEL_CONTROL_CLUSTER_ID:
        uplink_event.mHandler = AppTask::UplinkLevelControlEventHandler;
        GetAppTask().PostUplinkEvent(&uplink_event);
        break;

    case ZCL_IDENTIFY_CLUSTER_ID:
        uplink_event.mHandler = AppTask::UplinkIdentifyEventHandler;
        GetAppTask().PostUplinkEvent(&uplink_event);
        break;

    default:
        uplink_event.mHandler = NULL;
        break;
    }
}
