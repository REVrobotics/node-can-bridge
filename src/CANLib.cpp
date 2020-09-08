#include <iostream>
#include "CANBridge.h"
#include "napi.h"

c_CANBridge_ScanHandle CANHandle;

void scanForDevices() {
    if (CANHandle != NULL) {
        CANBridge_FreeScan(CANHandle);
    }

    CANHandle = CANBridge_Scan();
}

void cleanup() {
    if (CANHandle != NULL) {
        CANBridge_FreeScan(CANHandle);
    }
}

void printDeviceNames() {
    if (CANHandle == NULL) {
        scanForDevices();
    }

    int numDevices = CANBridge_NumDevices(CANHandle);
    for (int i=0;i<numDevices;i++) {
        std::cout << "\t" << CANBridge_GetDeviceName(CANHandle, i) << " - " << CANBridge_GetDeviceDescriptor(CANHandle, i) << " - " << CANBridge_GetDriverName(CANHandle, i) << std::endl;
    }
}

c_CANBridge_ScanHandle getDevices() {
    scanForDevices();
    return CANHandle;
}

Napi::Number getNumDevices(const Napi::CallbackInfo& info) {
    scanForDevices();
    int numDevices = CANBridge_NumDevices(CANHandle);
    Napi::Env env = info.Env();
    return Napi::Number::New(env, numDevices);
}

