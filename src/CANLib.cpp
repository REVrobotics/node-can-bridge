#include <iostream>
#include <rev/CANBridge.h>
#include <napi.h>
#include <thread>
#include <chrono>
#include "CANLib.h"

class CanWorker : public Napi::AsyncWorker {
    public:
        CanWorker(Napi::Function& callback)
        : AsyncWorker(callback) {}

        ~CanWorker() {}
    void Execute() override {
        c_CANBridge_ScanHandle CANHandle = CANBridge_Scan();
        std::cout << "Completed scan" << std::endl;
        numDevices = CANBridge_NumDevices(CANHandle);
        CANBridge_FreeScan(CANHandle);
        numDevices = 3;
    }

    void OnOK() override {
        Napi::HandleScope scope(Env());
        Callback().Call({Env().Null(), Napi::Number::New(Env(),numDevices)});
    }

    private:
    int numDevices = 0;
};


Napi::Value getNumDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Function cb = info[0].As<Napi::Function>();
    CanWorker* wk = new CanWorker(cb);
    wk->Queue();
    return info.Env().Undefined();
}