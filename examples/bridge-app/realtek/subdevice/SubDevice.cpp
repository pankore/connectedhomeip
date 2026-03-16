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

#include "SubDevice.h"
#include <lib/support/CHIPMemString.h>
#include <lib/support/CHIPMem.h>

SubDevice::SubDevice(const char * szSubDeviceName, const char * szLocation) : mState(kState_Off), mReachable(true)
{
    chip::Platform::CopyString(mName, szSubDeviceName);
    chip::Platform::CopyString(mLocation, szLocation);
    mEndpointId = 0;
}

bool SubDevice::IsOn() const
{
    return mState == kState_On;
}

bool SubDevice::IsReachable() const
{
    return mReachable;
}

void SubDevice::SetOnOff(bool aOn)
{
    if (aOn)
    {
        mState = kState_On;
    }
    else
    {
        mState = kState_Off;
    }
    if (mChanged_CB)
    {
        mChanged_CB(this, kChanged_State);
    }
}

void SubDevice::SetReachable(bool aReachable)
{
    mReachable = aReachable;
    if (mChanged_CB)
    {
        mChanged_CB(this, kChanged_Reachable);
    }
}

void SubDevice::SetName(const char * szSubDeviceName)
{
    chip::Platform::CopyString(mName, szSubDeviceName);
    if (mChanged_CB)
    {
        mChanged_CB(this, kChanged_Name);
    }
}

void SubDevice::SetLocation(const char * szLocation)
{
    chip::Platform::CopyString(mLocation, szLocation);
    if (mChanged_CB)
    {
        mChanged_CB(this, kChanged_Location);
    }
}

void SubDevice::SetChangeCallback(SubDeviceCallback_fn aChanged_CB)
{
    mChanged_CB = aChanged_CB;
}

// LightDevice implementation
LightDevice::LightDevice(const char * szSubDeviceName, const char * szLocation) : SubDevice(szSubDeviceName, szLocation)
{
}

// ThermostatDevice implementation
ThermostatDevice::ThermostatDevice(const char * szSubDeviceName, const char * szLocation)
    : SubDevice(szSubDeviceName, szLocation), mOccupiedCoolingSetpoint(2600), mOccupiedHeatingSetpoint(2000), mSystemMode(3)
{
}

int16_t ThermostatDevice::GetOccupiedCoolingSetpoint() const
{
    return mOccupiedCoolingSetpoint;
}

void ThermostatDevice::SetOccupiedCoolingSetpoint(int16_t value)
{
    mOccupiedCoolingSetpoint = value;
}

int16_t ThermostatDevice::GetOccupiedHeatingSetpoint() const
{
    return mOccupiedHeatingSetpoint;
}

void ThermostatDevice::SetOccupiedHeatingSetpoint(int16_t value)
{
    mOccupiedHeatingSetpoint = value;
}

uint8_t ThermostatDevice::GetSystemMode() const
{
    return mSystemMode;
}

void ThermostatDevice::SetSystemMode(uint8_t value)
{
    mSystemMode = value;
}