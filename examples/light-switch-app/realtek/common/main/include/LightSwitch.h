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

#include <app/util/basic-types.h>
#include <lib/core/CHIPError.h>

using namespace ::chip;

#if CONFIG_DEFAULT_ZAP
#define MAX_SUPPORTED_LIGHT_NUM 1
constexpr EndpointId kLightSwitchForGroupEndpointId                  = 1;
constexpr EndpointId kLightGenericSwitchEndpointId                   = 2;
constexpr EndpointId kLightSwitchEndpointId[MAX_SUPPORTED_LIGHT_NUM] = { 1 };
#elif CONFIG_1_TO_2_ZAP
#define MAX_SUPPORTED_LIGHT_NUM 2
constexpr EndpointId kLightSwitchForGroupEndpointId                  = 1;
constexpr EndpointId kLightGenericSwitchEndpointId                   = 4;
constexpr EndpointId kLightSwitchEndpointId[MAX_SUPPORTED_LIGHT_NUM] = { 2, 3 };
#elif CONFIG_1_TO_8_ZAP
#define MAX_SUPPORTED_LIGHT_NUM 8
constexpr EndpointId kLightSwitchForGroupEndpointId                  = 1;
constexpr EndpointId kLightGenericSwitchEndpointId                   = 10;
constexpr EndpointId kLightSwitchEndpointId[MAX_SUPPORTED_LIGHT_NUM] = { 2, 3, 4, 5, 6, 7, 8, 9 };
#elif CONFIG_1_TO_11_ZAP
#define MAX_SUPPORTED_LIGHT_NUM 11
constexpr EndpointId kLightSwitchForGroupEndpointId                  = 1;
constexpr EndpointId kLightGenericSwitchEndpointId                   = 10;
constexpr EndpointId kLightSwitchEndpointId[MAX_SUPPORTED_LIGHT_NUM] = { 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13 };
#endif

enum Action : uint8_t
{
    Toggle, /// Switch state on lighting-app device
    On,     /// Turn on light on lighting-app device
    Off     /// Turn off light on lighting-app device
};

enum LightCtrlType : uint8_t
{
    OnOff = 0x00,
    Level = 0x01,
    Color = 0x02,
    CT    = 0x03,
};

class LightSwitch
{
public:
    void Init();
    void InitiateActionSwitch(chip::EndpointId endpointId, uint8_t action);
    void DimmerChangeBrightness(chip::EndpointId endpointId, uint8_t brightness);
    void ColorChange(chip::EndpointId endpointId, uint16_t colorX, uint16_t colorY);
    void ColorTemperatureChange(chip::EndpointId endpointId, uint16_t colorTemperatureMireds);
    //void GenericSwitchInitialPress();
    //void GenericSwitchReleasePress();

    void SetLightCtrlType(LightCtrlType aCtrlType) { mCurrentCtrlType = aCtrlType; }
    LightCtrlType GetLightCtrlType() { return mCurrentCtrlType; }
    uint8_t GetNextBrightness()
    {
        mBrightnessIndex = (mBrightnessIndex + 1) % 4;
        return sBrightnessLevels[mBrightnessIndex];
    }
    uint16_t GetNextColorTemperature()
    {
        mColorTemperatureIndex = (mColorTemperatureIndex + 1) % 4;
        return sColorTemperatures[mColorTemperatureIndex];
    }
    uint16_t GetNextColorX()
    {
        mColorIndex = (mColorIndex + 1) % 7;
        return sColorX[mColorIndex];
    }
    uint16_t GetNextColorY()
    {
        mColorIndex = (mColorIndex + 1) % 7;
        return sColorY[mColorIndex];
    }

#if CONFIG_ENABLE_ATTRIBUTE_SUBSCRIBE
    void SubscribeRequestForOneNode(chip::EndpointId endpointId);
    void ShutdownSubscribeRequestForOneNode(chip::EndpointId endpointId);
#endif

    static LightSwitch & GetInstance()
    {
        static LightSwitch sLightSwitch;
        return sLightSwitch;
    }

private:
    LightCtrlType mCurrentCtrlType;
    uint8_t mBrightnessIndex;
    uint8_t mColorTemperatureIndex;
    uint8_t mColorIndex;

    static constexpr uint8_t sBrightnessLevels[4]   = { 25, 50, 125, 250 };
    static constexpr uint16_t sColorTemperatures[4] = { 6000, 12000, 30000, 60000 };
    static constexpr uint16_t sColorX[7]            = { 48141, 38928, 35194, 10867, 7733, 9266, 11387 };
    static constexpr uint16_t sColorY[7]            = { 17385, 26542, 30214, 48105, 23068, 5570, 328 };
};
