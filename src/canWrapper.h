#ifndef CAN_LIB
#define CAN_LIB
#include <napi.h>

void getDevices(const Napi::CallbackInfo& info);
void registerDeviceToHAL(const Napi::CallbackInfo& info);
void unregisterDeviceFromHAL(const Napi::CallbackInfo& info);
Napi::Object receiveMessage(const Napi::CallbackInfo& info);
Napi::Number openStreamSession(const Napi::CallbackInfo& info);
Napi::Array readStreamSession(const Napi::CallbackInfo& info);
Napi::Number closeStreamSession(const Napi::CallbackInfo& info);
Napi::Object getCANDetailStatus(const Napi::CallbackInfo& info);

#endif