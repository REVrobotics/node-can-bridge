#ifndef CAN_LIB
#define CAN_LIB
#include <napi.h>

Napi::Value getDevices(const Napi::CallbackInfo& info);
Napi::Value registerDeviceToHAL(const Napi::CallbackInfo& info);
void receiveMessage(const Napi::CallbackInfo& info);
void openStreamSession(const Napi::CallbackInfo& info);
Napi::Value readStreamSession(const Napi::CallbackInfo& info);
void closeStreamSession(const Napi::CallbackInfo& info);
void unregisterDeviceFromHAL(const Napi::CallbackInfo& info);
void getCANDetailStatus(const Napi::CallbackInfo& info);

#endif