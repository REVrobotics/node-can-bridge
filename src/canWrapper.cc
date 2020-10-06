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
#include <vector>
#include "canWrapper.h"

#define DEVICE_NOT_FOUND_ERROR "Device not found.  Make sure to run getDevices()"

rev::usb::CandleWinUSBDriver* driver = new rev::usb::CandleWinUSBDriver();
std::map<std::string, std::shared_ptr<rev::usb::CANDevice>> CANDeviceMap;

void removeExtraDevicesFromDeviceMap(std::vector<std::string> descriptors) {
    for (auto itr = CANDeviceMap.begin(); itr != CANDeviceMap.end(); ++itr) {
        bool inDevices = false;
        for(auto descriptor = descriptors.begin(); descriptor != descriptors.end(); ++descriptor) {
            if (*descriptor == itr->first){
                inDevices = true;
                break;
            }
        }
        if (!inDevices) {
            CANDeviceMap.erase(itr->first);
        }
    }
}

bool addDeviceToMap(std::string descriptor) {
    char* descriptor_chars = &descriptor[0];
    try {
        std::unique_ptr<rev::usb::CANDevice> canDevice = driver->CreateDeviceFromDescriptor(descriptor_chars);
        if (canDevice != nullptr) {
            CANDeviceMap[descriptor] = std::move(canDevice);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Params: none
// Returns:
//   devices: Array<{descriptor:string, name:string, driverName:string}
Napi::Array getDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    c_CANBridge_ScanHandle CANHandle = CANBridge_Scan();

    int numDevices = CANBridge_NumDevices(CANHandle);
    Napi::Array devices = Napi::Array::New(env);
    std::vector<std::string> descriptors;
    for (int i = 0; i < numDevices; i++) {
        std::string descriptor = CANBridge_GetDeviceDescriptor(CANHandle, i);
        std::string name = CANBridge_GetDeviceName(CANHandle, i);
        std::string driverName = CANBridge_GetDriverName(CANHandle, i);

        Napi::Object deviceInfo = Napi::Object::New(env);
        deviceInfo.Set("descriptor", descriptor);
        deviceInfo.Set("name", name);
        deviceInfo.Set("driverName", driverName);

        if (CANDeviceMap.find(descriptor) == CANDeviceMap.end()) {
            if (addDeviceToMap(descriptor)) {
                descriptors.push_back(descriptor);
                deviceInfo.Set("available", true);
            } else {
                deviceInfo.Set("available", false);
            }
        } else {
            descriptors.push_back(descriptor);
            deviceInfo.Set("available", true);
        }

        devices[i] = deviceInfo;
    }

    removeExtraDevicesFromDeviceMap(descriptors);
    CANBridge_FreeScan(CANHandle);
    return devices;
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
    if (deviceIterator == CANDeviceMap.end()) {
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
        return Napi::Object::New(env);
    }

    rev::usb::CANStatus status = deviceIterator->second->ReceiveCANMessage(message, messageId, messageMask);
    if (status != rev::usb::CANStatus::kOk) {
        Napi::Error::New(env, "Receiving message failed with status code " + std::to_string((int)status)).ThrowAsJavaScriptException();
        return Napi::Object::New(env);
    }

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
//   sessionHandle: Number
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
    if (deviceIterator == CANDeviceMap.end()) {
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    try {
        rev::usb::CANStatus status = deviceIterator->second->OpenStreamSession(&sessionHandle, filter, maxSize);
        if (status != rev::usb::CANStatus::kOk) {
            Napi::Error::New(env, "Opening stream session failed with error code "+(int)status).ThrowAsJavaScriptException();
        } else {
            return Napi::Number::New(env, sessionHandle);
        }
    } catch(...) {
        Napi::Error::New(env, "Opening stream session failed").ThrowAsJavaScriptException();
    }
    return Napi::Number::New(env, 0);
}

// Params:
//   descriptor: String
//   sessionHandle: number;
//   messagesToRead: Number
// Returns:
//   messages: Array<Object{messageID:Number, timeStamp:Number, data:Array<Number>}>
Napi::Array readStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t sessionHandle = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messagesToRead = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();
    HAL_CANStreamMessage *messages = new HAL_CANStreamMessage[messagesToRead];
    uint32_t messagesRead = 0;

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) {
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
        return Napi::Array::New(env);
    }

    try {
        deviceIterator->second->ReadStreamSession(sessionHandle, messages, messagesToRead, &messagesRead);
        Napi::HandleScope scope(env);
        Napi::Array messageArray = Napi::Array::New(env);
        for (uint32_t i = 0; i < messagesRead; i++) {
            Napi::HandleScope scope(env);
            Napi::Object message = Napi::Object::New(env);
            message.Set("messageID", messages[i].messageID);
            message.Set("timeStamp", messages[i].timeStamp);

            int messageLength = std::min((int)messages[i].dataSize, 8);
            Napi::Array data = Napi::Array::New(env, messageLength);
            for (int m = 0; m < messageLength; m++) {
                Napi::HandleScope scope(env);
                data[m] = Napi::Number::New(env, messages[i].data[m]);
            }
            message.Set("data", data);
            messageArray[i] = message;
        }
        delete[] messages;
        return messageArray;
    } catch(...) {
        delete[] messages;
        Napi::Error::New(env, "Reading stream session failed").ThrowAsJavaScriptException();
        return Napi::Array::New(env);
    }
}

// Params:
//   descriptor: String
//   sessionHandle: Number
// Returns:
//   status: Number
Napi::Number closeStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t sessionHandle = info[1].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[1].As<Napi::Function>();
    std::cout << "Closing";

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) {
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

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
    if (deviceIterator == CANDeviceMap.end()) {
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
        return Napi::Object::New(env);
    }

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

// Params:
//   descriptor: string
//   messageId: Number
//   messageData: Number[]
//   repeatPeriod: Number
// Returns:
//   status: Number
Napi::Number sendCANMessage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    Napi::Array dataParam = info[2].As<Napi::Array>();
    int repeatPeriodMs = info[3].As<Napi::Number>().Uint32Value();

    auto deviceIterator = CANDeviceMap.find(descriptor);
    if (deviceIterator == CANDeviceMap.end()) {
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    uint8_t messageData[8];
    for (int i = 0; i < dataParam.Length(); i++) {
        messageData[i] = dataParam.Get(i).As<Napi::Number>().Uint32Value();
    }

    rev::usb::CANMessage* message = new rev::usb::CANMessage(messageId, messageData, dataParam.Length());
    rev::usb::CANStatus status = deviceIterator->second->SendCANMessage(*message, repeatPeriodMs);
    return Napi::Number::New(env, (int)status);
}