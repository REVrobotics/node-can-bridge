#ifndef CAN_LIB
#define CAN_LIB
#include <napi.h>

void getDevices(const Napi::CallbackInfo& info);
Napi::Number registerDeviceToHAL(const Napi::CallbackInfo& info);
void unregisterDeviceFromHAL(const Napi::CallbackInfo& info);
Napi::Object receiveMessage(const Napi::CallbackInfo& info);
Napi::Object receiveHalMessage(const Napi::CallbackInfo& info);
Napi::Number openStreamSession(const Napi::CallbackInfo& info);
Napi::Array readStreamSession(const Napi::CallbackInfo& info);
Napi::Number closeStreamSession(const Napi::CallbackInfo& info);
Napi::Object getCANDetailStatus(const Napi::CallbackInfo& info);
Napi::Number sendCANMessage(const Napi::CallbackInfo& info);
Napi::Number sendRtrMessage(const Napi::CallbackInfo& info);
Napi::Number sendCANMessageThroughHal(const Napi::CallbackInfo& info);
Napi::Number sendHALMessage(const Napi::CallbackInfo& info);
void initializeNotifier(const Napi::CallbackInfo& info);
void waitForNotifierAlarm(const Napi::CallbackInfo& info);
void stopNotifier(const Napi::CallbackInfo& info);
Napi::Promise writeDfuToBin(const Napi::CallbackInfo& info);
Napi::Array getImageElements(const Napi::CallbackInfo& info);
Napi::Number openHALStreamSession(const Napi::CallbackInfo& info);
Napi::Array readHALStreamSession(const Napi::CallbackInfo& info);
void closeHALStreamSession(const Napi::CallbackInfo& info);
void setThreadPriority(const Napi::CallbackInfo& info);
void setSparkMaxHeartbeatData(const Napi::CallbackInfo& info);
void startRevCommonHeartbeat(const Napi::CallbackInfo& info);
void stopHeartbeats(const Napi::CallbackInfo& info);
void ackHeartbeats(const Napi::CallbackInfo& info);
Napi::Object getTimestampsForAllReceivedMessages(const Napi::CallbackInfo& info);
#endif
