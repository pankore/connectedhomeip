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

#include "SubDevice.h"
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/Span.h>
#include <app-common/zap-generated/ids/Clusters.h>

class SubDeviceManager
{
public:
    static SubDeviceManager & GetInstance()
    {
        static SubDeviceManager instance;
        return instance;
    }

    void AddDevice(SubDevice * dev);
    void RemoveDevice(SubDevice * dev);
    SubDevice * GetDevice(uint16_t index);
    void HandleDeviceStatusChanged(SubDevice * dev, SubDevice::Changed_t itemChangedMask);

    // Thermostat writable attributes (only for Thermostat device)
    inline uint8_t GetThermostatSystemMode() const { return mThermostatSystemMode; };
    inline void SetThermostatSystemMode(uint8_t mode) { mThermostatSystemMode = mode; };
    inline int16_t GetThermostatCoolingSetpoint() const { return mThermostatCoolingSetpoint; };
    inline void SetThermostatCoolingSetpoint(int16_t temp) { mThermostatCoolingSetpoint = temp; };
    inline int16_t GetThermostatHeatingSetpoint() const { return mThermostatHeatingSetpoint; };
    inline void SetThermostatHeatingSetpoint(int16_t temp) { mThermostatHeatingSetpoint = temp; };

private:
    SubDeviceManager(){};
    // Thermostat writable attributes
    uint8_t mThermostatSystemMode = 3;           // Default: Cool mode
    int16_t mThermostatCoolingSetpoint = 0x0BB8; // 30°C = 0x0BB8
    int16_t mThermostatHeatingSetpoint = 0x07D0; // 20°C = 0x07D0
};

int AddDeviceEndpoint(SubDevice * dev, EmberAfEndpointType * ep, const chip::Span<const EmberAfDeviceType> & deviceTypeList,
                      const chip::Span<chip::DataVersion> & dataVersionStorage, chip::EndpointId parentEndpointId);
CHIP_ERROR RemoveDeviceEndpoint(SubDevice * dev);
void HandleDeviceStatusChanged(SubDevice * dev, SubDevice::Changed_t itemChangedMask);
void Init_Bridge_Endpoint();
void Sync_SubDevice_test();