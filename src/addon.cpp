#include <napi.h>
#include <CANLib.cpp>
#include "CANBridge.h"

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "getNumDevices"),
                Napi::Function::New(env, getNumDevices));
    return exports;
}

NODE_API_MODULE(addon, Init);