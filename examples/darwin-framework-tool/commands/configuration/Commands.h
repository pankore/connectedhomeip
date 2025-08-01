/*
 *   Copyright (c) 2024 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#pragma once

#import <Matter/Matter.h>

#include "PrintDeviceCommand.h"
#include "ResetMRPParametersCommand.h"
#include "SetMRPParametersCommand.h"
#include "SetUpDeviceCommand.h"

void registerCommandsConfiguration(Commands & commands)
{
    const char * clusterName = "Configuration";

    commands_list clusterCommands = {
        make_unique<SetUpDeviceCommand>(),
        make_unique<PrintDeviceCommand>(),
        make_unique<SetMRPParametersCommand>(),
        make_unique<ResetMRPParametersCommand>(),
    };

    commands.RegisterCommandSet(clusterName, clusterCommands,
                                "Commands for reading/configuring various state of the Matter framework or a device.");
}
