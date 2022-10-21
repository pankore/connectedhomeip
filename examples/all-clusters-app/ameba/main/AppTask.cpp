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
#include "freertos/FreeRTOS.h"

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-id.h>

#define APP_TASK_NAME "APP"
#define APP_EVENT_QUEUE_SIZE 10
#define APP_TASK_STACK_SIZE (2048)
#define BUTTON_PRESSED 1
#define APP_LIGHT_SWITCH 1

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

LEDWidget AppLED;
Button AppButton;

namespace {
constexpr EndpointId kLightEndpointId = 1;
QueueHandle_t UplinkEventQueue;
QueueHandle_t DownlinkEventQueue;
TaskHandle_t UplinkTaskHandle;
TaskHandle_t DownlinkTaskHandle;
} // namespace

AppTask AppTask::sAppTask;

CHIP_ERROR AppTask::Init()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    AppLED.Init();
    AppButton.Init();

    // Need to create a class, replace esp's Button class, or maybe just use the LEDWidget class
    // Set a callbackhere, replace esp's Button callback
    AppButton.SetButtonPressCallback(DownlinkCallback);
    // don't need to set uplink callback, it is MatterPostAttributeChangeCallback 

    return err;
}

CHIP_ERROR AppTask::StartDownlinkTask()
{
    DownlinkEventQueue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(AppEvent));
    if (DownlinkEventQueue == NULL)
    {
        ChipLogError(DeviceLayer, "Failed to allocate downlink event queue");
        //return APP_ERROR_EVENT_QUEUE_FAILED;
        // return appropriate error code
    }

    // Start Downlink task.
    BaseType_t xReturned;
    xReturned = xTaskCreate(DownlinkTask, APP_TASK_NAME, APP_TASK_STACK_SIZE, NULL, 1, &DownlinkTaskHandle);
    // return (xReturned == pdPASS) ? CHIP_NO_ERROR : APP_ERROR_CREATE_TASK_FAILED;
    // return appropriate error cde
}

void AppTask::DownlinkTask(void * pvParameter)
{
    AppEvent event;
    // move sAppTask.Init() out of DownlinkTask, call from chipinterface
    // CHIP_ERROR err = sAppTask.Init();
    // if (err != CHIP_NO_ERROR)
    // {
    //     ChipLogError("AppTask.Init() failed");
    //     return;
    // }

    ChipLogProgress(DeviceLayer, "Downlink Task started");

    // Loop here and keep listening on the queue for Downlink (Firmware application to matter)
    while (true)
    {
        BaseType_t eventReceived = xQueueReceive(DownlinkEventQueue, &event, pdMS_TO_TICKS(10));
        while (eventReceived == pdTRUE)
        {
            sAppTask.DispatchEvent(&event);
            eventReceived = xQueueReceive(DownlinkEventQueue, &event, 0); // return immediately if the queue is empty
        }
    }
}

void AppTask::PostDownlinkEvent(const AppEvent * aEvent)
{
    if (DownlinkEventQueue != NULL)
    {
        BaseType_t status;
        if (xPortInIsrContext())
        {
            BaseType_t higherPrioTaskWoken = pdFALSE;
            status                         = xQueueSendFromISR(DownlinkEventQueue, aEvent, &higherPrioTaskWoken);
        }
        else
        {
            status = xQueueSend(DownlinkEventQueue, aEvent, 1);
        }
        if (!status)
            ChipLogError(DeviceLayer, "Failed to post downlink event to downlink event queue");
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

// We need 1 callback, 1 callback handler
// Change this callback handler to our own
void AppTask::DownlinkEventHandler(AppEvent * aEvent)
{
    // Do we need to turn on LED here? actually depends on vendor
    // If they switch/press a button (or through their own application), LED turns on immediately, before going downlink to update matter
    // then we don't need to turn on LED here again
    // If they switch/press a button (or through their own application), LED doesn't turn on yet, go downlink to update matter
    // then we need to turn on LED here
    AppLED.Toggle();
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    sAppTask.UpdateClusterState();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

// We need 1 callback, 1 callback handler
// Change this callback to our own
void AppTask::DownlinkCallback()
{
    AppEvent downlink_event;
    downlink_event.Type     = AppEvent::kEventType_Uplink;
    downlink_event.mHandler = AppTask::DownlinkEventHandler;
    sAppTask.PostDownlinkEvent(&downlink_event);
}

void AppTask::UpdateClusterState()
{
    ESP_LOGI(TAG, "Writing to OnOff cluster");
    // write the new on/off value
    EmberAfStatus status = Clusters::OnOff::Attributes::OnOff::Set(kLightEndpointId, AppLED.IsTurnedOn());

    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        ESP_LOGE(TAG, "Updating on/off cluster failed: %x", status);
    }

    ESP_LOGI(TAG, "Writing to Current Level cluster");
    status = Clusters::LevelControl::Attributes::CurrentLevel::Set(kLightEndpointId, AppLED.GetLevel());

    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        ESP_LOGE(TAG, "Updating level cluster failed: %x", status);
    }
}

CHIP_ERROR AppTask::StartUplinkTask()
{
    UplinkEventQueue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(AppEvent));
    if (UplinkEventQueue == NULL)
    {
        ChipLogError(DeviceLayer, "Failed to allocate uplink event queue");
        //return APP_ERROR_EVENT_QUEUE_FAILED;
        // return appropriate error code
    }

    // Start Downlink task.
    BaseType_t xReturned;
    xReturned = xTaskCreate(UplinkTask, APP_TASK_NAME, APP_TASK_STACK_SIZE, NULL, 1, &UplinkTaskHandle);
    // return (xReturned == pdPASS) ? CHIP_NO_ERROR : APP_ERROR_CREATE_TASK_FAILED;
    // return appropriate error cde
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
            sAppTask.DispatchEvent(&event);
            eventReceived = xQueueReceive(UplinkEventQueue, &event, 0); // return immediately if the queue is empty
        }
    }
}

void AppTask::PostUplinkEvent(const AppEvent * aEvent)
{
    if (UplinkEventQueue != NULL)
    {
        BaseType_t status;
        // don't need this check for uplink? or even downlink? since it won't be from ISR
        if (xPortInIsrContext())
        {
            BaseType_t higherPrioTaskWoken = pdFALSE;
            status                         = xQueueSendFromISR(UplinkEventQueue, aEvent, &higherPrioTaskWoken);
        }
        else
        {
            status = xQueueSend(UplinkEventQueue, aEvent, 1);
        }
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

void AppTask::UplinkEventHandler(AppEvent * aEvent)
{
    // Here we turn on the LED
    // No need to update cluster state
}

// This is the uplink callback
void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & path, uint8_t type, uint16_t size, uint8_t * value)
{
    AppEvent uplink_event;
    uplink_event.endpointId = path.mEndpointId;
    uplink_event.clusterId = path.mClusterId;
    uplink_event.attributeId = path.mAttributeId;
    uplink_event.value = value;
    uplink_event.mHandler = AppTask::UplinkEventHandler;
    sAppTask.PostUplinkEvent(&uplink_event);

    // chip::DeviceManager::CHIPDeviceManagerCallbacks * cb =
    //     chip::DeviceManager::CHIPDeviceManager::GetInstance().GetCHIPDeviceManagerCallbacks();
    // if (cb != nullptr)
    // {
    //     cb->PostAttributeChangeCallback(path.mEndpointId, path.mClusterId, path.mAttributeId, type, size, value);
    // }

    // From here, we prepare and post an event to the uplink queue
}

