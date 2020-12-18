/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "USBMtpInterface.h"

#define EP_IN 0
#define EP_OUT 1
#define EP_INT 2

USBMtpInterface::USBMtpInterface(int index, UsbInterfaceDesc *info)
{
    interface_index = index;
    info->interface_desc = &mtp_interface_descriptor;
    info->endpoint_desc[EP_IN] = &mtp_endpoint_descriptor_in;
    info->endpoint_desc[EP_OUT] = &mtp_endpoint_descriptor_out;
    info->endpoint_desc[EP_INT] = &mtp_endpoint_descriptor_interrupt;
    info->string_descriptor = mtp_string_descriptor;
}

USBMtpInterface::~USBMtpInterface() {
}

ssize_t USBMtpInterface::read(char *ptr, size_t len)
{
    return usbTransfer(interface_index, EP_OUT, UsbDirection_Read, (void*)ptr, len, 1000000000LL);
}
ssize_t USBMtpInterface::write(const char *ptr, size_t len)
{
    return usbTransfer(interface_index, EP_IN, UsbDirection_Write, (void*)ptr, len, UINT64_MAX);
}
ssize_t USBMtpInterface::sendEvent(const char *ptr, size_t len)
{
    return usbTransfer(interface_index, EP_INT, UsbDirection_Write, (void*)ptr, len, UINT64_MAX);
}