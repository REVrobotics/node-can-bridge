#include <napi.h>
#include "canWrapper.h"

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "getDevices"),
                Napi::Function::New(env, getDevices));
    exports.Set(Napi::String::New(env, "registerDeviceToHAL"),
                Napi::Function::New(env, registerDeviceToHAL));
    exports.Set(Napi::String::New(env, "unregisterDeviceFromHAL"),
                Napi::Function::New(env, unregisterDeviceFromHAL));
    exports.Set(Napi::String::New(env, "receiveMessage"),
                Napi::Function::New(env, receiveMessage));
    exports.Set(Napi::String::New(env, "openStreamSession"),
                Napi::Function::New(env, openStreamSession));
    exports.Set(Napi::String::New(env, "readStreamSession"),
                Napi::Function::New(env, readStreamSession));
    exports.Set(Napi::String::New(env, "closeStreamSession"),
                Napi::Function::New(env, closeStreamSession));
    exports.Set(Napi::String::New(env, "getCANDetailStatus"),
                Napi::Function::New(env, getCANDetailStatus));
    exports.Set(Napi::String::New(env, "sendCANMessage"),
                Napi::Function::New(env, sendCANMessage));
    exports.Set(Napi::String::New(env, "waitForNotifierAlarm"),
                Napi::Function::New(env, waitForNotifierAlarm));
    return exports;
}

NODE_API_MODULE(addon, Init);