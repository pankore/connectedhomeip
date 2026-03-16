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
#include "SubDeviceManager.h"
#include "../common/main/include/Globals.h"
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Commands.h>
#include <app/ConcreteAttributePath.h>
#include <app/InteractionModelEngine.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/reporting/reporting.h>
#include <app/util/attribute-storage.h>
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

// Device type and version definitions
#define DEVICE_TYPE_LO_ON_OFF_LIGHT (0x0100)
#define DEVICE_TYPE_THERMOSTAT (0x0301)
#define DEVICE_TYPE_BRIDGED_NODE (0x0013)
#define DEVICE_VERSION_DEFAULT (0)

static const int kNodeLabelSize = 32;
// Current ZCL implementation of Struct uses a max-size array of 254 bytes
static const int kDescriptorAttributeArraySize = 254;

/**
 * Cluster attribute definitions
 */

// On/Off cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, 0), /* on/off */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* device list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* server list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* client list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),  /* parts list */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Bridged Device Basic information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize, 0), /* NodeLabel */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),              /* Reachable */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Thermostat cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(thermostatAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Thermostat::Attributes::LocalTemperature::Id, TEMPERATURE, 2, 0),                         /* LocalTemperature */
    DECLARE_DYNAMIC_ATTRIBUTE(Thermostat::Attributes::OccupiedCoolingSetpoint::Id, INT16S, 2, MATTER_ATTRIBUTE_FLAG_WRITABLE),               /* OccupiedCoolingSetpoint */
    DECLARE_DYNAMIC_ATTRIBUTE(Thermostat::Attributes::OccupiedHeatingSetpoint::Id, INT16S, 2, MATTER_ATTRIBUTE_FLAG_WRITABLE),              /* OccupiedHeatingSetpoint */
    DECLARE_DYNAMIC_ATTRIBUTE(Thermostat::Attributes::SystemMode::Id, ENUM8, 1, MATTER_ATTRIBUTE_FLAG_WRITABLE), /* SystemMode */
    DECLARE_DYNAMIC_ATTRIBUTE(Thermostat::Attributes::FeatureMap::Id, INT32U, 4, 0),                          /* FeatureMap */
    DECLARE_DYNAMIC_ATTRIBUTE(Thermostat::Attributes::ClusterRevision::Id, INT16U, 2, 0),                     /* ClusterRevision */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Fan Control cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(fanControlAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::FanMode::Id, ENUM8, 1, 0),     /* FanMode */
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::FanModeSequence::Id, ENUM8, 1, 0), /* FanModeSequence */
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::PercentSetting::Id, INT8U, 1, 0), /* PercentSetting */
    DECLARE_DYNAMIC_ATTRIBUTE(FanControl::Attributes::PercentCurrent::Id, INT8U, 1, 0), /* PercentCurrent */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Thermostat User Interface Configuration cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(thermostatUIAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ThermostatUserInterfaceConfiguration::Attributes::TemperatureDisplayMode::Id, ENUM8, 1, 0),     /* TemperatureDisplayMode */
    DECLARE_DYNAMIC_ATTRIBUTE(ThermostatUserInterfaceConfiguration::Attributes::KeypadLockout::Id, ENUM8, 1, 0),            /* KeypadLockout */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

/**
 * Cluster command definitions
 */

constexpr CommandId onOffIncomingCommands[] = {
    app::Clusters::OnOff::Commands::Off::Id,
    app::Clusters::OnOff::Commands::On::Id,
    app::Clusters::OnOff::Commands::Toggle::Id,
    app::Clusters::OnOff::Commands::OffWithEffect::Id,
    app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Id,
    app::Clusters::OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};

// Thermostat cluster commands
constexpr CommandId thermostatIncomingCommands[] = {
    app::Clusters::Thermostat::Commands::SetpointRaiseLower::Id,
    app::Clusters::Thermostat::Commands::GetWeeklySchedule::Id,
    app::Clusters::Thermostat::Commands::SetWeeklySchedule::Id,
    app::Clusters::Thermostat::Commands::ClearWeeklySchedule::Id,
    app::Clusters::Thermostat::Commands::SetActiveScheduleRequest::Id,
    kInvalidCommandId,
};

/**
 * Cluster list definitions
 */

// Bridged Light endpoint clusters
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedLightClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

// Bridged Thermostat endpoint clusters
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedThermostatClusters)
DECLARE_DYNAMIC_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Thermostat::Id, thermostatAttrs, ZAP_CLUSTER_MASK(SERVER), thermostatIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ThermostatUserInterfaceConfiguration::Id, thermostatUIAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(FanControl::Id, fanControlAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

/**
 * Endpoint type definitions
 */

DECLARE_DYNAMIC_ENDPOINT(bridgedLightEndpoint, bridgedLightClusters);
DECLARE_DYNAMIC_ENDPOINT(bridgedThermostatEndpoint, bridgedThermostatClusters);

/**
 * Device type definitions
 */

const EmberAfDeviceType gBridgedLightDeviceTypes[] = { { DEVICE_TYPE_LO_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
                                                       { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

const EmberAfDeviceType gBridgedThermostatDeviceTypes[] = { { DEVICE_TYPE_THERMOSTAT, DEVICE_VERSION_DEFAULT },
                                                            { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

/**
 * BridgedDeviceManager class - Manages all bridged devices
 */
class BridgedDeviceManager
{
public:
    // Singleton instance
    static BridgedDeviceManager & GetInstance()
    {
        static BridgedDeviceManager instance;
        return instance;
    }

    // Initialize all bridged devices
    void Init()
    {
        // Initialize bridge endpoint structure
        Init_Bridge_Endpoint();

        // Add all bridged devices
        AddBridgedDevices();
    }

private:
    // Device instances
    LightDevice mLight;
    ThermostatDevice mThermostat;

    // Data versions for each device
    DataVersion mLightDataVersions[MATTER_ARRAY_SIZE(bridgedLightClusters)];
    DataVersion mThermostatDataVersions[MATTER_ARRAY_SIZE(bridgedThermostatClusters)];

    BridgedDeviceManager() : mLight("Light 1", "Office"),
                             mThermostat("Thermostat 1", "Living Room")
    {
    }

    // Add all bridged devices to the bridge
    void AddBridgedDevices()
    {
        // Add Light device
        AddLightDevice();

        // Add Thermostat device
        AddThermostatDevice();
    }

    // Add Light device (On/Off Light)
    void AddLightDevice()
    {
        mLight.SetReachable(true);

        // Set callback for state changes
        mLight.SetChangeCallback([](SubDevice * dev, SubDevice::Changed_t changed) {
            HandleDeviceStatusChanged(dev, changed);

            if (changed & SubDevice::kChanged_State) {
                lightStatusLED.Set(dev->IsOn());
                ChipLogProgress(DeviceLayer, "Light state: %s", dev->IsOn() ? "ON" : "OFF");
            }
        });

        // Add the bridged light device
        int ret = AddDeviceEndpoint(&mLight, &bridgedLightEndpoint,
                                    Span<const EmberAfDeviceType>(gBridgedLightDeviceTypes),
                                    Span<DataVersion>(mLightDataVersions), 1);

        if (ret < 0)
        {
            ChipLogProgress(DeviceLayer, "Failed to add bridged light device");
        }
        else
        {
            ChipLogProgress(DeviceLayer, "Added bridged light device at endpoint %d", mLight.GetEndpointId());
        }
    }

    // Add Thermostat device
    void AddThermostatDevice()
    {
        mThermostat.SetReachable(true);

        // Set callback for state changes
        mThermostat.SetChangeCallback([](SubDevice * dev, SubDevice::Changed_t changed) {
            HandleDeviceStatusChanged(dev, changed);

            if (changed & SubDevice::kChanged_State) {
                lightStatusLED.Set(dev->IsOn());
                ChipLogProgress(DeviceLayer, "Thermostat state: %s", dev->IsOn() ? "ON" : "OFF");
            }
        });

        // Add the bridged thermostat device
        int ret = AddDeviceEndpoint(&mThermostat, &bridgedThermostatEndpoint,
                                   Span<const EmberAfDeviceType>(gBridgedThermostatDeviceTypes),
                                   Span<DataVersion>(mThermostatDataVersions), 1);

        if (ret < 0)
        {
            ChipLogProgress(DeviceLayer, "Failed to add bridged thermostat device");
        }
        else
        {
            ChipLogProgress(DeviceLayer, "Added bridged thermostat device at endpoint %d", mThermostat.GetEndpointId());
        }
    }
};

// External function to initialize bridged devices
void Sync_SubDevice_test()
{
    ChipLogProgress(DeviceLayer, "Sync_SubDevice_test");
    BridgedDeviceManager::GetInstance().Init();
}