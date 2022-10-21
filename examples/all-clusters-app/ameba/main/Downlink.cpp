/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include "Downlink.h"

#define GPIO_IRQ_PIN PA_12

// do this inside of h file
static Downlink::DownlinkCallback downlink_handler = nullptr;

static void gpio_irq_handler(void * arg)
{
    if (downlink_handler != nullptr)
    {
        downlink_handler();
    }
}

void Downlink::Init()
{
    gpio_irq_t gpio_btn;
    
    // Initial Push Button pin as interrupt source
    gpio_irq_init(&gpio_btn, GPIO_IRQ_PIN, gpio_irq_handler, 1);
    gpio_irq_set(&gpio_btn, IRQ_FALL, 1);   // Falling Edge Trigger
    gpio_irq_enable(&gpio_btn) ;
}

void Downlink::SetDownlinkCallback(DownlinkCallback downlink_callback)
{
    if (downlink_callback != nullptr)
    {
        downlink_handler = downlink_callback;
    }
}
