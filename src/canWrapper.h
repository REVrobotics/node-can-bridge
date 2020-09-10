#ifndef CAN_LIB
#define CAN_LIB
#include <napi.h>

Napi::Value getDevices(const Napi::CallbackInfo& info);
Napi::Value registerDeviceToHAL(const Napi::CallbackInfo& info);

#endif