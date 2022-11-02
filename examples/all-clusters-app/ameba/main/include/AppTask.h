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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "AppEvent.h"
#include "LEDWidget.h"
#include <platform/CHIPDeviceLayer.h>

// Application-defined error codes in the CHIP_ERROR space.
#define APP_ERROR_EVENT_QUEUE_FAILED CHIP_APPLICATION_ERROR(0x01)
#define APP_ERROR_CREATE_TASK_FAILED CHIP_APPLICATION_ERROR(0x02)
#define APP_ERROR_UNHANDLED_EVENT CHIP_APPLICATION_ERROR(0x03)
#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)
#define APP_ERROR_START_TIMER_FAILED CHIP_APPLICATION_ERROR(0x05)
#define APP_ERROR_STOP_TIMER_FAILED CHIP_APPLICATION_ERROR(0x06)

extern LEDWidget AppLED;

class AppTask
{

public:
    CHIP_ERROR Init();
    CHIP_ERROR StartUplinkTask();
    CHIP_ERROR StartDownlinkTask();
    static void UplinkTask(void * pvParameter);
    static void DownlinkTask(void * pvParameter);
    void PostUplinkEvent(const AppEvent * event);
    void PostDownlinkEvent(const AppEvent * event);
    void UpdateClusterState(AppEvent * event);

    static void UplinkOnOffEventHandler(AppEvent * aEvent);
    static void UplinkLevelControlEventHandler(AppEvent * aEvent);
    static void UplinkIdentifyEventHandler(AppEvent * aEvent);

    static void DownlinkOnOffEventHandler(AppEvent * aEvent);

private:
    friend AppTask & GetAppTask(void);
    void DispatchUplinkEvent(AppEvent * event);
    void DispatchDownlinkEvent(AppEvent * event);
    static void DownlinkOnOffCallback();

    static AppTask sAppTask;
};

inline AppTask & GetAppTask(void)
{
    return AppTask::sAppTask;
}
