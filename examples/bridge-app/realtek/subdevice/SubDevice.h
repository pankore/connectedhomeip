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

#pragma once

// These are the bridged devices
#include <app/util/attribute-storage.h>
#include <functional>
#include <stdbool.h>
#include <stdint.h>

class SubDevice
{
public:
    static const int kSubDeviceNameSize     = 32;
    static const int kSubDeviceLocationSize = 32;

    enum State_t
    {
        kState_On = 0,
        kState_Off,
    } State;

    enum Changed_t
    {
        kChanged_Reachable = 0x01,
        kChanged_State     = 0x02,
        kChanged_Location  = 0x04,
        kChanged_Name      = 0x08,
    } Changed;

    virtual ~SubDevice() = default;
    SubDevice(const char * szSubDeviceName, const char * szLocation);

    bool IsOn() const;
    bool IsReachable() const;
    void SetOnOff(bool aOn);
    void SetReachable(bool aReachable);
    void SetName(const char * szSubDeviceName);
    void SetLocation(const char * szLocation);
    inline void SetEndpointId(chip::EndpointId id) { mEndpointId = id; };
    inline chip::EndpointId GetEndpointId() { return mEndpointId; };
    inline char * GetName() { return mName; };
    inline char * GetLocation() { return mLocation; };

    enum DeviceType
    {
        kDeviceType_Light     = 0,
        kDeviceType_Thermostat = 1,
    };

    virtual DeviceType GetDeviceType() const = 0;

    using SubDeviceCallback_fn = std::function<void(SubDevice *, Changed_t)>;
    void SetChangeCallback(SubDeviceCallback_fn aChanged_CB);

protected:
    State_t mState;
    bool mReachable;
    char mName[kSubDeviceNameSize];
    char mLocation[kSubDeviceLocationSize];
    chip::EndpointId mEndpointId;
    SubDeviceCallback_fn mChanged_CB;
};

// LightDevice - for lighting devices
class LightDevice : public SubDevice
{
public:
    LightDevice(const char * szSubDeviceName, const char * szLocation);

    DeviceType GetDeviceType() const override { return kDeviceType_Light; }
};

// ThermostatDevice - for thermostat devices
class ThermostatDevice : public SubDevice
{
public:
    ThermostatDevice(const char * szSubDeviceName, const char * szLocation);

    DeviceType GetDeviceType() const override { return kDeviceType_Thermostat; }

    // Thermostat attributes
    int16_t GetOccupiedCoolingSetpoint() const;
    void SetOccupiedCoolingSetpoint(int16_t value);
    int16_t GetOccupiedHeatingSetpoint() const;
    void SetOccupiedHeatingSetpoint(int16_t value);
    uint8_t GetSystemMode() const;
    void SetSystemMode(uint8_t value);

private:
    int16_t mOccupiedCoolingSetpoint;
    int16_t mOccupiedHeatingSetpoint;
    uint8_t mSystemMode;
};