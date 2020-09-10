#include <napi.h>
#include "canWrapper.h"

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "getDevices"),
                Napi::Function::New(env, getDevices));
    return exports;
}

NODE_API_MODULE(addon, Init);