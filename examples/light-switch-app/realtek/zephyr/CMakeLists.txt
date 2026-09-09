#
#    Copyright (c) 2022 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#
cmake_minimum_required(VERSION 3.20)

get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/connectedhomeip REALPATH)
get_filename_component(REALTEK_COMMON ${CHIP_ROOT}/examples/platform/realtek REALPATH)
get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)

if(EXISTS "${CHIP_ROOT}/src/platform/realtek/zephyr/${BOARD}.overlay")
  set(GLOBAL_DTC_OVERLAY_FILE "${CHIP_ROOT}/src/platform/realtek/zephyr/${BOARD}.overlay")
else()
  unset(GLOBAL_DTC_OVERLAY_FILE)
endif()

if(DTC_OVERLAY_FILE)
  set(DTC_OVERLAY_FILE
    "${DTC_OVERLAY_FILE} ${GLOBAL_DTC_OVERLAY_FILE}"
    CACHE STRING "" FORCE
  )
else()
  set(DTC_OVERLAY_FILE ${GLOBAL_DTC_OVERLAY_FILE})
endif()

set(CONF_FILE prj.conf)

# Load NCS/Zephyr build system
list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/realtek/chip-module)
find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(chip-realtek-light-switch-example)

include(${CHIP_ROOT}/config/realtek/app/enable-gnu-std.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_compile_options(app PRIVATE -fpermissive)

target_include_directories(app PRIVATE
                           main/include
                           ${GEN_DIR}/app-common
                           ${GEN_DIR}/light-switch-app
                           ${REALTEK_COMMON}/bee/ota
                           ${REALTEK_COMMON}/zephyr
                           ${REALTEK_COMMON}/util
                           ${ZEPHYR_BASE}/../modules/hal/realtek/bee/drivers/wdt/inc
                           ${ZEPHYR_BASE}/../realtek-zephyr-project/applications/matter_common)

target_sources(app PRIVATE
               main/main.cpp
               main/AppTask.cpp
               main/ZclCallbacks.cpp
               main/LightSwitch.cpp
               main/BindingHandler.cpp
               ${REALTEK_COMMON}/zephyr/LEDWidget.cpp
               ${REALTEK_COMMON}/zephyr/matter_button.c
               ${ZEPHYR_BASE}/../realtek-zephyr-project/applications/ot-cli/src/alarm.c)
zephyr_code_relocate(FILES ${ZEPHYR_BASE}/../realtek-zephyr-project/applications/ot-cli/src/alarm.c LOCATION ITCM_TEXT)
zephyr_code_relocate(FILES ${ZEPHYR_BASE}/modules/openthread/platform/radio.c LOCATION ITCM_TEXT)

target_link_options(app
              PUBLIC
                  -Wl,--wrap,memcpy
                  -Wl,--wrap,memset
                  -Wl,--wrap,mac_EnterCritical_rom
                  -Wl,--wrap,mac_ExitCritical_rom
                  -Wl,--wrap,otPlatReset
                  -Wl,--wrap,platformAlarmInit
                  -Wl,--wrap,platformAlarmProcess
                  -Wl,--wrap,otPlatAlarmMilliGetNow
                  -Wl,--wrap,otPlatAlarmMilliStartAt
                  -Wl,--wrap,otPlatAlarmMilliStop
                  -Wl,--wrap,otPlatAlarmMicroGetNow
                  -Wl,--wrap,otPlatAlarmMicroStartAt
                  -Wl,--wrap,otPlatAlarmMicroStop
                  -Wl,--wrap,otPlatTimeGet
                  -Wl,--wrap,otPlatLog
          )

chip_configure_data_model(app
    INCLUDE_SERVER
    ZAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../light-switch-common/light-switch-app.zap
)

if(CONFIG_CHIP_OTA_REQUESTOR)
  target_sources(app PRIVATE
                 ${REALTEK_COMMON}/bee/ota/OTAInitializer.cpp
                 ${ZEPHYR_BASE}/../realtek-zephyr-project/applications/matter_common/matter_ota.c)
endif()

# Fix for unused swap parameter in: zephyr/include/zephyr/arch/riscv/irq.h:70
add_compile_options(-Wno-error=unused-parameter)
