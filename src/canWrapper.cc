#include <iostream>
#include <rev/CANBridge.h>
#include <rev/CANMessage.h>
#include <rev/CANStatus.h>
#include <rev/CANBridgeUtils.h>
#include <rev/Drivers/CandleWinUSB/CandleWinUSBDriver.h>
#include <rev/Drivers/CandleWinUSB/CandleWinUSBDevice.h>
#include <hal/HAL.h>
#include <hal/CAN.h>
#include <napi.h>
#include <thread>
#include <chrono>
#include <map>
#include "canWrapper.h"

#define DEVICE_NOT_FOUND_ERROR "Device not found.  Make sure to run getDevices()"
#define STREAM_NOT_FOUND_ERROR "Stream session handle not found for the requested device"


rev::usb::CandleWinUSBDriver* driver = new rev::usb::CandleWinUSBDriver();
std::map<std::string, std::shared_ptr<rev::usb::CANDevice>> CANDeviceMap;
std::map<std::string, uint32_t> streamSessionHandleMap;

void clearDeviceMap() {
    for (auto itr = CANDeviceMap.begin(); itr != CANDeviceMap.end(); ++itr) {
        delete &itr->second;
    }
    CANDeviceMap.clear();

    // TODO: we probably don't want to have to restart stream sessions every time we check for devices
    streamSessionHandleMap.clear();
}

void addDeviceToMap(std::string descriptor) {
    char* descriptor_chars = &descriptor[0];
    std::unique_ptr<rev::usb::CANDevice> canDevice = driver->CreateDeviceFromDescriptor(descriptor_chars);
    if (canDevice != nullptr)
        CANDeviceMap[descriptor] = std::move(canDevice);
}

void addStreamSessionHandle(std::string descriptor, uint32_t handle) {
    auto streamHandleIterator = streamSessionHandleMap.find(descriptor);
    if (streamHandleIterator != streamSessionHandleMap.end()) {
        auto deviceIterator = CANDeviceMap.find(descriptor);
        if (deviceIterator != CANDeviceMap.end()) deviceIterator->second->CloseStreamSession(streamHandleIterator->second);
    }
    streamSessionHandleMap[descriptor] = handle;
}

uint32_t getStreamHandleFromMap(std::string descriptor) {
    auto iterator = streamSessionHandleMap.find(descriptor);
    if (iterator == streamSessionHandleMap.end()) {
        return NULL;
    }
    return iterator->second;
}

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

        clearDeviceMap();
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
            addDeviceToMap(descriptor);
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
void getDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Function cb = info[0].As<Napi::Function>();
    GetDevicesWorker* wk = new GetDevicesWorker(cb);
    wk->Queue();
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
//   descriptor: Number
//   messageId: Number
//   messageMask: Number
// Returns:
//   status: Number
void registerDeviceToHAL(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    char* descriptor_chars = &descriptor[0];

    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    RegisterDeviceToHALWorker* wk = new RegisterDeviceToHALWorker(cb, descriptor_chars, messageId, messageMask);
    wk->Queue();
}

// Params:
//   descriptor: Number
// Returns:
//   status: Number
void unregisterDeviceFromHAL(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    char* descriptor_chars = &descriptor[0];
    Napi::Function cb = info[1].As<Napi::Function>();

    try {
        CANBridge_UnregisterDeviceFromHAL(descriptor_chars);
        cb.Call(env.Global(), {env.Null(), Napi::Number::New(env, (int)rev::usb::CANStatus::kOk)});
    } catch (...) {
        cb.Call(env.Global(), {Napi::Number::New(env, (int)rev::usb::CANStatus::kError)});
    }
}

// Params:
//   descriptor: Number
//   messageId: Number
//   messageMask: Number
// Returns:
//   data: Object{data:Number[], messageID:number, timeStamp:number}
Napi::Object receiveMessage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    std::shared_ptr<rev::usb::CANMessage> message;

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();

    rev::usb::CANStatus status = deviceIterator->second->ReceiveCANMessage(message, messageId, messageMask);
    if (status != rev::usb::CANStatus::kOk)
        Napi::Error::New(env, "Receiving message failed with status code " + std::to_string((int)status)).ThrowAsJavaScriptException();

    size_t messageSize = message->GetSize();
    const uint8_t* messageData = message->GetData();
    Napi::Array napiMessage = Napi::Array::New(env, messageSize);
    for (int i = 0; i < messageSize; i++) {
        napiMessage[i] =  messageData[i];
    }
    Napi::Object messageInfo = Napi::Object::New(env);
    messageInfo.Set("messageID", message->GetMessageId());
    messageInfo.Set("timeStamp", message->GetTimestampUs());
    messageInfo.Set("data", napiMessage);

    return messageInfo;
}

// Params:
//   descriptor: String
//   messageId: Number
//   messageMask: Number
//   maxSize: Number
// Returns:
//   status: Number
Napi::Number openStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    uint32_t maxSize = info[3].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[4].As<Napi::Function>();

    rev::usb::CANBridge_CANFilter filter;
    filter.messageId = messageId;
    filter.messageMask = messageMask;
    uint32_t sessionHandle;

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();

    rev::usb::CANStatus status = deviceIterator->second->OpenStreamSession(&sessionHandle, filter, maxSize);
    if (status == rev::usb::CANStatus::kOk) addStreamSessionHandle(descriptor, sessionHandle);
    return Napi::Number::New(env, (int)status);
}

// Params:
//   descriptor: String
//   messagesToRead: Number
// Returns:
//   messages: Array<Object{messageID:Number, timeStamp:Number, data:Array<Number>}>
Napi::Array readStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messagesToRead = info[1].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[2].As<Napi::Function>();

    HAL_CANStreamMessage* messages;
    uint32_t messagesRead = 0;

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
    uint32_t sessionHandle = getStreamHandleFromMap(descriptor);
    if (sessionHandle == NULL) Napi::Error::New(env, STREAM_NOT_FOUND_ERROR).ThrowAsJavaScriptException();

    deviceIterator->second->ReadStreamSession(sessionHandle, messages, messagesToRead, &messagesRead);

    Napi::Array messageArray = Napi::Array::New(env, messagesRead);
    for (uint32_t i = 0; i < messagesRead; i++) {
        Napi::Object message = Napi::Object::New(env);
        message.Set("messageID", messages[i].messageID);
        message.Set("timeStamp", messages[i].timeStamp);

        size_t messageLength = std::min((int)messages[i].dataSize, 8);
        Napi::Array data = Napi::Array::New(env, messageLength);
        for (int m = 0; m < messageLength; m++) {
            data[m] = Napi::Number::New(env, messages[i].data[m]);
        }
        message.Set("data", data);
        messageArray[i] = message;
    }
    return messageArray;
}

// Params:
//   descriptor: String
// Returns:
//   status: Number
Napi::Number closeStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    Napi::Function cb = info[1].As<Napi::Function>();

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
    uint32_t sessionHandle = getStreamHandleFromMap(descriptor);
    if (sessionHandle == NULL) Napi::Error::New(env, STREAM_NOT_FOUND_ERROR).ThrowAsJavaScriptException();

    rev::usb::CANStatus status = deviceIterator->second->CloseStreamSession(sessionHandle);
    return Napi::Number::New(env, (int)status);
}

// Params:
//   descriptor: String
// Returns:
//   status: Object{percentBusUtilization:Number, busOff:Number, txFull:Number, receiveErr:Number, transmitError:Number}
Napi::Object getCANDetailStatus(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    Napi::Function cb = info[1].As<Napi::Function>();

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();

    float percentBusUtilization;
    uint32_t busOff;
    uint32_t txFull;
    uint32_t receiveErr;
    uint32_t transmitErr;
    deviceIterator->second->GetCANDetailStatus(&percentBusUtilization, &busOff, &txFull, &receiveErr, &transmitErr);

    Napi::Object status = Napi::Object::New(env);
    status.Set("percentBusUtilization", percentBusUtilization);
    status.Set("busOff", busOff);
    status.Set("txFull", txFull);
    status.Set("receiveErr", receiveErr);
    status.Set("transmitErr", transmitErr);
    return status;
}