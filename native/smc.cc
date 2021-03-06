/*
 * Apple System Management Control (SMC) Tool
 * Copyright (C) 2006 devnull
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef BUILDING_NODE_EXTENSION
    #define BUILDING_NODE_EXTENSION
#endif

#include <stdio.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <node.h>
#include <v8.h>

#include "smc.h"

using namespace v8;

static io_connect_t conn;

UInt32 _strtoul(char *str, int size, int base) {
    UInt32 total = 0;
    int i;

    for (i = 0; i < size; i++) {
        if (base == 16)
            total += str[i] << (size - 1 - i) * 8;
        else
           total += (unsigned char) (str[i] << (size - 1 - i) * 8);
    }
    return total;
}

float _strtof(char *str, int size, int e) {
    float total = 0;
    int i;

    for (i = 0; i < size; i++) {
        if (i == (size - 1))
           total += (str[i] & 0xff) >> e;
        else
           total += str[i] << (size - 1 - i) * (8 - e);
    }

    return total;
}

void _ultostr(char *str, UInt32 val) {
    str[0] = '\0';
    sprintf(str, "%c%c%c%c",
            (unsigned int) val >> 24,
            (unsigned int) val >> 16,
            (unsigned int) val >> 8,
            (unsigned int) val);
}

kern_return_t SMCOpen(void) {
    kern_return_t result;
    mach_port_t   masterPort;
    io_iterator_t iterator;
    io_object_t   device;

    result = IOMasterPort(MACH_PORT_NULL, &masterPort);

    CFMutableDictionaryRef matchingDictionary = IOServiceMatching("AppleSMC");
    result = IOServiceGetMatchingServices(masterPort, matchingDictionary, &iterator);
    if (result != kIOReturnSuccess) {
        printf("Error: IOServiceGetMatchingServices() = %08x\n", result);
        return 1;
    }

    device = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (device == 0) {
        printf("Error: no SMC found\n");
        return 1;
    }

    result = IOServiceOpen(device, mach_task_self(), 0, &conn);
    IOObjectRelease(device);
    if (result != kIOReturnSuccess) {
        printf("Error: IOServiceOpen() = %08x\n", result);
        return 1;
    }

    return kIOReturnSuccess;
}

kern_return_t SMCClose() {
    return IOServiceClose(conn);
}


kern_return_t SMCCall(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure) {
    size_t   structureInputSize;
    size_t   structureOutputSize;

    structureInputSize = sizeof(SMCKeyData_t);
    structureOutputSize = sizeof(SMCKeyData_t);

    #if MAC_OS_X_VERSION_10_5
    return IOConnectCallStructMethod( conn, index,
                            // inputStructure
                            inputStructure, structureInputSize,
                            // ouputStructure
                            outputStructure, &structureOutputSize );
    #else
    return IOConnectMethodStructureIStructureO( conn, index,
                                                structureInputSize, /* structureInputSize */
                                                &structureOutputSize,   /* structureOutputSize */
                                                inputStructure,        /* inputStructure */
                                                outputStructure);       /* ouputStructure */
    #endif

}

kern_return_t SMCReadKey(UInt32Char_t key, SMCVal_t *val) {
    kern_return_t result;
    SMCKeyData_t  inputStructure;
    SMCKeyData_t  outputStructure;

    memset(&inputStructure, 0, sizeof(SMCKeyData_t));
    memset(&outputStructure, 0, sizeof(SMCKeyData_t));
    memset(val, 0, sizeof(SMCVal_t));

    inputStructure.key = _strtoul(key, 4, 16);
    inputStructure.data8 = SMC_CMD_READ_KEYINFO;

    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;

    val->dataSize = outputStructure.keyInfo.dataSize;
    _ultostr(val->dataType, outputStructure.keyInfo.dataType);
    inputStructure.keyInfo.dataSize = val->dataSize;
    inputStructure.data8 = SMC_CMD_READ_BYTES;

    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;

    memcpy(val->bytes, outputStructure.bytes, sizeof(outputStructure.bytes));

    return kIOReturnSuccess;
}

double SMCGetTemperature() {
    SMCVal_t val;
    kern_return_t result;

    result = SMCReadKey((char*) SMC_KEY_CPU_TEMP, &val);
    if (result == kIOReturnSuccess) {
        // read succeeded - check returned value
        if (val.dataSize > 0) {
            if (strcmp(val.dataType, DATATYPE_SP78) == 0) {
                // convert fp78 value to temperature
                int intValue = (val.bytes[0] * 256 + val.bytes[1]) >> 2;
                return intValue / 64.0;
            }
        }
    }
    // read failed
    return 0.0;
}

int SMCGetFanNumber() {
    SMCVal_t val;
    kern_return_t result;

    result = SMCReadKey((char*) SMC_KEY_FAN_NUMBER, &val);
    if (result == kIOReturnSuccess) {
        // read succeeded - check returned value
        if (val.dataSize > 0) {
            if (strcmp(val.dataType, DATATYPE_UINT8) == 0) {
                int intValue = _strtoul((char *)val.bytes, val.dataSize, 10);
                return intValue;
            }
        }
    }
    // read failed
    return 0;
}

int SMCGetFanRPM(int fan_number) {
    SMCVal_t val;
    kern_return_t result;
    UInt32Char_t key;

    sprintf(key, SMC_PKEY_FAN_RPM, fan_number);

    result = SMCReadKey(key, &val);
    if (result == kIOReturnSuccess) {
        // read succeeded - check returned value
        if (val.dataSize > 0) {
            if (strcmp(val.dataType, DATATYPE_FPE2) == 0) {
                int intValue = _strtof(val.bytes, val.dataSize, 2);
                return intValue;
            }
        }
    }
    // read failed
    return 0;
}

v8::Handle<Value> Temperature(const Arguments& args) {
    HandleScope scope;
    SMCOpen();
    double temperature = SMCGetTemperature();
    SMCClose();
    return scope.Close(Number::New(temperature));
}

v8::Handle<Value> Fans(const Arguments& args) {
    HandleScope scope;
    SMCOpen();
    int numberOfFans = SMCGetFanNumber();
    SMCClose();
    return scope.Close(Number::New(numberOfFans));
}

v8::Handle<Value> FanRpm(const Arguments& args) {
    HandleScope scope;
    if (args.Length() < 1) {
        //Fan number (id) isn't specified
        return scope.Close(Undefined());
    }
    if (!args[0]->IsNumber()) {
        ThrowException(Exception::TypeError(String::New("Expected number")));
        return scope.Close(Undefined());
    }
    int fanNumber = args[0]->Int32Value();
    SMCOpen();
    int rpm = SMCGetFanRPM(fanNumber);
    SMCClose();
    return scope.Close(Number::New(rpm));
}

void Init(v8::Handle<Object> exports) {
  exports->Set(String::NewSymbol("temperature"),
      FunctionTemplate::New(Temperature)->GetFunction());
  exports->Set(String::NewSymbol("fans"),
      FunctionTemplate::New(Fans)->GetFunction());
  exports->Set(String::NewSymbol("fanRpm"),
      FunctionTemplate::New(FanRpm)->GetFunction());
}

NODE_MODULE(smc, Init)
