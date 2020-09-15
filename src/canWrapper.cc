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

class DeviceNotFoundException: public std::exception
{
  virtual const char* what() const throw()
  {
    return DEVICE_NOT_FOUND_ERROR;
  }
} DeviceNotFound;

class StreamNotFoundException: public std::exception
{
  virtual const char* what() const throw()
  {
    return "Stream session handle not found for the requested device";
  }
} StreamNotFound;

rev::usb::CandleWinUSBDriver* driver = new rev::usb::CandleWinUSBDriver();
std::map<std::string, std::shared_ptr<rev::usb::CANDevice>> CANDeviceMap;
std::map<std::string, uint32_t> streamSessionHandleMap;
std::mutex mtx;

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
Napi::Value getDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Function cb = info[0].As<Napi::Function>();
    GetDevicesWorker* wk = new GetDevicesWorker(cb);
    wk->Queue();

    return info.Env().Undefined();
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
Napi::Value registerDeviceToHAL(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    char* descriptor_chars = &descriptor[0];

    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    RegisterDeviceToHALWorker* wk = new RegisterDeviceToHALWorker(cb, descriptor_chars, messageId, messageMask);
    wk->Queue();
    return info.Env().Undefined();
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
//   data: Array
void receiveMessage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    std::shared_ptr<rev::usb::CANMessage> message;

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) {
        cb.Call(env.Global(), {Napi::String::New(env, DEVICE_NOT_FOUND_ERROR)});
        return;
    }

    rev::usb::CANStatus status = deviceIterator->second->ReceiveCANMessage(message, messageId, messageMask);
    if (status != rev::usb::CANStatus::kOk) {
        cb.Call(env.Global(), {Napi::String::New(env, "Receiving message failed with status code " + std::to_string((int)status))});
        return;
    }

    size_t messageSize = message->GetSize();
    const uint8_t* messageData = message->GetData();
    Napi::Array napiMessage = Napi::Array::New(env, messageSize);
    for (int i = 0; i < messageSize; i++) {
        napiMessage[i] =  messageData[i];
    }

    cb.Call(env.Global(), {env.Null(), napiMessage});
}

// Params:
//   descriptor: String
//   messageId: Number
//   messageMask: Number
//   maxSize: Number
// Returns:
//   status: Number
void openStreamSession(const Napi::CallbackInfo& info) {
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
    if (deviceIterator == CANDeviceMap.end()) {
        cb.Call(env.Global(), {Napi::String::New(env, DEVICE_NOT_FOUND_ERROR)});
        return;
    }

    rev::usb::CANStatus status = deviceIterator->second->OpenStreamSession(&sessionHandle, filter, maxSize);
    if (status == rev::usb::CANStatus::kOk) addStreamSessionHandle(descriptor, sessionHandle);
    cb.Call(env.Global(), {env.Null(), Napi::Number::New(env, (int)status)});
}

class ReadStreamSessionWorker : public Napi::AsyncWorker {
    public:
        ReadStreamSessionWorker(Napi::Function& callback, uint32_t messagesRead, HAL_CANStreamMessage* messages)
        : AsyncWorker(callback), messagesRead(messagesRead), messages(messages) {}

    void Execute() override {
        messagesJSON = "[";
        for (uint32_t i = 0; i < messagesRead; i++) {
            messagesJSON += "{messageID: " + std::to_string(messages[i].messageID) + ", timeStamp: " + std::to_string(messages[i].timeStamp) + ", data:[";
            size_t messageLength = std::min((int)messages[i].dataSize, 8);
            for (int m = 0; m < messageLength; m++) {
                messagesJSON += std::to_string((int)messages[i].data[m]);
                if (m < messageLength-1) messagesJSON+=", ";
            }
            messagesJSON += "]}";
            if (i < messagesRead-1) messagesJSON += ", ";
        }

        messagesJSON  += "]";
    }

    void OnOK() override {
        Napi::HandleScope scope(Env());
        Callback().Call({Env().Null(), Napi::String::New(Env(), messagesJSON)});
    }

    void OnError(const Napi::Error& e) override {
        std::cout << e.what() << std::endl;
    }

    private:
        std::string messagesJSON;
        uint32_t messagesRead;
        HAL_CANStreamMessage* messages;
};

// Params:
//   descriptor: String
//   messagesToRead: Number
// Returns:
//   messages: Array<Array<Number>>
Napi::Value readStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messagesToRead = info[1].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[2].As<Napi::Function>();

    auto deviceIterator = CANDeviceMap.find(descriptor);
            if (deviceIterator == CANDeviceMap.end()) throw DeviceNotFound;
            uint32_t sessionHandle = getStreamHandleFromMap(descriptor);
            if (sessionHandle == NULL) throw StreamNotFound;

            HAL_CANStreamMessage* messages;
            uint32_t messagesRead = 0;
                    deviceIterator->second->ReadStreamSession(sessionHandle, messages, messagesToRead, &messagesRead);


    ReadStreamSessionWorker* wk = new ReadStreamSessionWorker(cb, messagesRead, messages);
    wk->Queue();
    return info.Env().Undefined();
}

// Params:
//   descriptor: String
// Returns:
//   status: Number
void closeStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    Napi::Function cb = info[1].As<Napi::Function>();

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) {
        cb.Call(env.Global(), {Napi::String::New(env, DEVICE_NOT_FOUND_ERROR)});
        return;
    }

    uint32_t sessionHandle = getStreamHandleFromMap(descriptor);
    if (sessionHandle == NULL) {
        cb.Call(env.Global(), {Napi::String::New(env, "Stream not started")});
        return;
    }

    rev::usb::CANStatus status = deviceIterator->second->CloseStreamSession(sessionHandle);
    cb.Call(env.Global(), {env.Null(), Napi::Number::New(env, (int)status)});
}