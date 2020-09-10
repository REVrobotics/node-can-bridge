#include <iostream>
#include <rev/CANBridge.h>
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


Napi::Value getDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Function cb = info[0].As<Napi::Function>();
    GetDevicesWorker* wk = new GetDevicesWorker(cb);
    wk->Queue();
    return info.Env().Undefined();
}