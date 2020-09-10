#include <iostream>
#include <rev/CANBridge.h>
#include <hal/HAL.h>
#include <napi.h>
#include <thread>
#include <chrono>
#include "canWrapper.h"

class GetDevicesWorker : public Napi::AsyncWorker {
    public:
        GetDevicesWorker(Napi::Function& callback)
        : AsyncWorker(callback) {}

        ~GetDevicesWorker() {}

    void Execute() override {
        CANHandle = CANBridge_Scan();
    }

    void OnOK() override {
        Napi::HandleScope scope(Env());
        int numDevices = CANBridge_NumDevices(CANHandle);
        devices = Napi::Array::New(Env(), numDevices);
        for (int i = 0; i < numDevices; i++) {
            std::string descriptor = CANBridge_GetDeviceDescriptor(CANHandle, i);
            std::string name = CANBridge_GetDeviceName(CANHandle, i);
            std::string driverName = CANBridge_GetDriverName(CANHandle, i);

            Napi::Object deviceInfo = Napi::Object::New(Env());
            deviceInfo.Set("descriptor", descriptor);
            deviceInfo.Set("name", name);
            deviceInfo.Set("driverName", driverName);

            devices[i] = deviceInfo;
        }

        CANBridge_FreeScan(CANHandle);
        Callback().Call({Env().Null(), devices});
    }

    private:
        c_CANBridge_ScanHandle CANHandle;
        Napi::Array devices;
};

// Params: none
// Returns:
//   devices: Array<{descriptor:string, name:string, driverName:string}
Napi::Value getDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Function cb = info[0].As<Napi::Function>();
    GetDevicesWorker* wk = new GetDevicesWorker(cb);
    wk->Queue();

    return info.Env().Undefined();
}

class RegisterDeviceToHALWorker : public Napi::AsyncWorker {
    public:
        RegisterDeviceToHALWorker(Napi::Function& callback, char* descriptor, uint32_t messageId, uint32_t messageMask)
        : AsyncWorker(callback), descriptor(descriptor), messageId(messageId), messageMask(messageMask) {}

        ~RegisterDeviceToHALWorker() {}

    void Execute() override {
        HAL_Initialize(500, 0);
        CANBridge_RegisterDeviceToHAL(descriptor, messageId, messageMask, &status);
    }

    void OnOK() override {
        Napi::HandleScope scope(Env());
        Callback().Call({Env().Null(), Napi::Number::New(Env(), status)});
    }

    private:
        char* descriptor;
        uint32_t messageId;
        uint32_t messageMask;
        int32_t status = -1;
};

// Params:
//   descriptor: string
//   messageId: uint32_t
//   messageMask: uint32_t
// Returns:
//   status: int32_t
Napi::Value registerDeviceToHAL(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    char* descriptor_chars = &descriptor[0];

    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    RegisterDeviceToHALWorker* wk = new RegisterDeviceToHALWorker(cb, descriptor_chars, messageId, messageMask);
    wk->Queue();
    return info.Env().Undefined();
}