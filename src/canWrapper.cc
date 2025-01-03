#include <iostream>
#include <rev/CANBridge.h>
#include <rev/CANMessage.h>
#include <rev/CANStatus.h>
#include <rev/CANBridgeUtils.h>
#include <rev/Drivers/CandleWinUSB/CandleWinUSBDriver.h>
#include <rev/Drivers/CandleWinUSB/CandleWinUSBDevice.h>
#include <utils/ThreadUtils.h>
#include <hal/HAL.h>
#include <hal/CAN.h>
#include <napi.h>
#include <thread>
#include <chrono>
#include <map>
#include <array>
#include <vector>
#include <set>
#include <exception>
#include <mutex>
#include <ctime>
#include "canWrapper.h"
#include "DfuSeFile.h"

#define REV_COMMON_HEARTBEAT_ID 0x00502C0
#define SPARK_HEARTBEAT_ID 0x2052C80
#define HEARTBEAT_PERIOD_MS 20

#define SPARK_HEARTBEAT_LENGTH 8
#define REV_COMMON_HEARTBEAT_LENGTH 1
uint8_t disabledSparkHeartbeat[] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t disabledRevCommonHeartbeat[] = {0};

rev::usb::CandleWinUSBDriver* driver = new rev::usb::CandleWinUSBDriver();

std::set<std::string> devicesRegisteredToHal; // TODO(Noah): Protect with mutex
bool halInitialized = false;
uint32_t m_notifier;

std::mutex canDevicesMtx;
// These values should only be accessed while holding canDevicesMtx
std::map<std::string, std::shared_ptr<rev::usb::CANDevice>> canDeviceMap;

std::mutex watchdogMtx;
// These values should only be accessed while holding watchdogMtx
std::vector<std::string> heartbeatsRunning;
bool heartbeatTimeoutExpired = true; // Should only be changed in heartbeatsWatchdog()
std::map<std::string, std::array<uint8_t, REV_COMMON_HEARTBEAT_LENGTH>> revCommonHeartbeatMap;
std::map<std::string, std::array<uint8_t, SPARK_HEARTBEAT_LENGTH>> sparkHeartbeatMap;
auto latestHeartbeatAck = std::chrono::time_point<std::chrono::steady_clock>();

void throwDeviceNotFoundError(Napi::Env env) {
    Napi::Error error = Napi::Error::New(env, "CAN bridge device not found. Make sure to run getDevices()");
    error.Set("canBridgeDeviceNotFound", Napi::Boolean::New(env, true));
    error.ThrowAsJavaScriptException();
}

// Only call when holding canDevicesMtx
void removeExtraDevicesFromDeviceMap(std::vector<std::string> descriptors) {
    for (auto itr = canDeviceMap.begin(); itr != canDeviceMap.end(); ++itr) {
        bool inDevices = false;
        for(auto descriptor = descriptors.begin(); descriptor != descriptors.end(); ++descriptor) {
            if (*descriptor == itr->first){
                inDevices = true;
                break;
            }
        }
        if (!inDevices) {
            canDeviceMap.erase(itr->first);
        }
    }
}

// Only call when holding canDevicesMtx
bool addDeviceToMap(std::string descriptor) {
    char* descriptor_chars = &descriptor[0];
    try {
        std::unique_ptr<rev::usb::CANDevice> canDevice = driver->CreateDeviceFromDescriptor(descriptor_chars);
        if (canDevice != nullptr) {
            canDeviceMap[descriptor] = std::move(canDevice);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

class GetDevicesWorker : public Napi::AsyncWorker {
    public:
        GetDevicesWorker(Napi::Function& callback)
        : Napi::AsyncWorker(callback) {}

        ~GetDevicesWorker() {}

    void Execute() override {
        std::scoped_lock lock{canDevicesMtx};

        CANHandle = CANBridge_Scan();
        numDevices = CANBridge_NumDevices(CANHandle);
        std::vector<std::string> descriptors;
        for (int i = 0; i < numDevices; i++) {
            std::string descriptor = CANBridge_GetDeviceDescriptor(CANHandle, i);

            if (canDeviceMap.find(descriptor) == canDeviceMap.end()) {
                if (addDeviceToMap(descriptor)) {
                    descriptors.push_back(descriptor);
                    isDeviceAvailable.push_back(true);
                } else  {
                    isDeviceAvailable.push_back(false);
                }
            } else {
                descriptors.push_back(descriptor);
                isDeviceAvailable.push_back(true);
            }
        }
        removeExtraDevicesFromDeviceMap(descriptors);
    }

    void OnOK() override {
        Napi::HandleScope scope(Env());
        Napi::Array devices = Napi::Array::New(Env());
        for (int i = 0; i < numDevices; i++) {
            std::string descriptor = CANBridge_GetDeviceDescriptor(CANHandle, i);
            std::string name = CANBridge_GetDeviceName(CANHandle, i);
            std::string driverName = CANBridge_GetDriverName(CANHandle, i);

            Napi::Object deviceInfo = Napi::Object::New(Env());
            deviceInfo.Set("descriptor", descriptor);
            deviceInfo.Set("name", name);
            deviceInfo.Set("driverName", driverName);
            deviceInfo.Set("available", Napi::Boolean::New(Env(), isDeviceAvailable[i]));

            devices[i] = deviceInfo;
        }

        CANBridge_FreeScan(CANHandle);
        Callback().Call({Env().Null(), devices});
    }

    private:
        c_CANBridge_ScanHandle CANHandle;
        int numDevices;
        std::vector<bool> isDeviceAvailable;
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


// Params:
//   descriptor: Number
//   messageId: Number
//   messageMask: Number
// Returns:
//   status: Number
Napi::Number registerDeviceToHAL(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    char* descriptor_chars = &descriptor[0];

    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    if (!halInitialized) {
        HAL_Initialize(500, 0);
        halInitialized = true;
    }

    { // This block exists to define how long we hold canDevicesMtx
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator != canDeviceMap.end()) {
            canDeviceMap.erase(deviceIterator->first);
        }
    }

    int32_t status;
    CANBridge_RegisterDeviceToHAL(descriptor_chars, messageId, messageMask, &status);
    if (status == 0) devicesRegisteredToHal.insert(descriptor);
    return Napi::Number::New(env, status);
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
        if (devicesRegisteredToHal.find(descriptor) != devicesRegisteredToHal.end())
            devicesRegisteredToHal.erase(devicesRegisteredToHal.find(descriptor));
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
    std::shared_ptr<rev::usb::CANDevice> device;

    { // This block exists to define how long we hold canDevicesMtx
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) {
            if (devicesRegisteredToHal.find(descriptor) != devicesRegisteredToHal.end()) return receiveHalMessage(info);
            throwDeviceNotFoundError(env);
            return Napi::Object::New(env);
        }

        device = deviceIterator->second;
    }

    rev::usb::CANStatus status = device->ReceiveCANMessage(message, messageId, messageMask);
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
// Returns:
//   data: Object{data:Number[], messageID:number, timeStamp:number}
Napi::Object receiveHalMessage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    uint8_t data[8];
    uint8_t dataSize = 0;
    uint32_t timeStamp = 0;
    int32_t status;
    HAL_CAN_ReceiveMessage(&messageId, messageMask, data, &dataSize, &timeStamp, &status);

    Napi::Array napiMessage = Napi::Array::New(env, dataSize);
    for (int i = 0; i < dataSize; i++) {
        napiMessage[i] =  data[i];
    }

    Napi::Object messageInfo = Napi::Object::New(env);
    messageInfo.Set("messageID", messageId);
    messageInfo.Set("timeStamp", timeStamp);
    messageInfo.Set("data", napiMessage);

    return messageInfo;
}

// Params:
//   descriptor: String
//   priority: Number
void setThreadPriority(const Napi::CallbackInfo& info) {
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t priority = info[1].As<Napi::Number>().Uint32Value();

    std::scoped_lock lock{canDevicesMtx};
    auto deviceIterator = canDeviceMap.find(descriptor);
    if (deviceIterator == canDeviceMap.end()) return;
    deviceIterator->second->setThreadPriority(static_cast<rev::usb::utils::ThreadPriority>(priority));
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

    std::shared_ptr<rev::usb::CANDevice> device;

    { // This block exists to define how long we hold canDevicesMtx
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) {
            throwDeviceNotFoundError(env);
            return Napi::Number::New(env, 0);
        }

        device = deviceIterator->second;
    }

    try {
        rev::usb::CANStatus status = device->OpenStreamSession(&sessionHandle, filter, maxSize);
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

    std::shared_ptr<rev::usb::CANDevice> device;

    { // This block exists to define how long we hold canDevicesMtx
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) {
            throwDeviceNotFoundError(env);
            return Napi::Array::New(env);
        }

        device = deviceIterator->second;
    }

    try {
        device->ReadStreamSession(sessionHandle, messages, messagesToRead, &messagesRead);
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

    std::scoped_lock lock{canDevicesMtx};
    auto deviceIterator = canDeviceMap.find(descriptor);
    if (deviceIterator == canDeviceMap.end()) {
        throwDeviceNotFoundError(env);
        return Napi::Number::New(env, 0);
    }

    rev::usb::CANStatus status = deviceIterator->second->CloseStreamSession(sessionHandle);
    return Napi::Number::New(env, (int)status);
}

// Params:
//   descriptor: String
// Returns:
//   status: Object{percentBusUtilization:Number, busOff:Number, txFull:Number, receiveErr:Number, transmitError:Number, lastErrorTime:Number}
Napi::Object getCANDetailStatus(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    Napi::Function cb = info[1].As<Napi::Function>();

    std::shared_ptr<rev::usb::CANDevice> device;

    { // This block exists to define how long we hold canDevicesMtx
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) {
            throwDeviceNotFoundError(env);
            return Napi::Object::New(env);
        }

        device = deviceIterator->second;
    }

    float percentBusUtilization;
    uint32_t busOff;
    uint32_t txFull;
    uint32_t receiveErr;
    uint32_t transmitErr;
    uint32_t lastErrorTime;
    device->GetCANDetailStatus(&percentBusUtilization, &busOff, &txFull, &receiveErr, &transmitErr, &lastErrorTime);

    Napi::Object status = Napi::Object::New(env);
    status.Set("percentBusUtilization", percentBusUtilization);
    status.Set("busOff", busOff);
    status.Set("txFull", txFull);
    status.Set("receiveErr", receiveErr);
    status.Set("transmitErr", transmitErr);
    status.Set("lastErrorTime", lastErrorTime);
    return status;
}

int _sendCANMessage(std::string descriptor, uint32_t messageId, uint8_t* messageData, int dataSize, int repeatPeriodMs) {
    std::shared_ptr<rev::usb::CANDevice> device;
    bool foundDevice = false;

    { // This block exists to define how long we hold canDevicesMtx
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator != canDeviceMap.end()) {
            device = deviceIterator->second;
            foundDevice = true;
        }
    }

    if (!foundDevice) {
        if (devicesRegisteredToHal.find(descriptor) != devicesRegisteredToHal.end()) {
            int32_t status;
            HAL_CAN_SendMessage(messageId, messageData, dataSize, repeatPeriodMs, &status);
            return status;
        }
        return -1;
    }

    rev::usb::CANMessage* message = new rev::usb::CANMessage(messageId, messageData, dataSize);
    rev::usb::CANStatus status = device->SendCANMessage(*message, repeatPeriodMs);
    return (int)status;
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

    uint8_t messageData[8];
    for (uint32_t i = 0; i < dataParam.Length(); i++) {
        messageData[i] = dataParam.Get(i).As<Napi::Number>().Uint32Value();
    }
    int status = _sendCANMessage(descriptor, messageId, messageData, dataParam.Length(), repeatPeriodMs);
    if (status < 0) {
        throwDeviceNotFoundError(env);
    }
    return Napi::Number::New(env, status);
}


// Params:
//   descriptor: string
//   messageId: Number
//   messageData: Number[]
//   repeatPeriod: Number
// Returns:
//   status: Number
Napi::Number sendRtrMessage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    Napi::Array dataParam = info[2].As<Napi::Array>();
    int repeatPeriodMs = info[3].As<Napi::Number>().Uint32Value();

    messageId |= HAL_CAN_IS_FRAME_REMOTE;

    uint8_t messageData[8];
    for (uint32_t i = 0; i < dataParam.Length(); i++) {
        messageData[i] = dataParam.Get(i).As<Napi::Number>().Uint32Value();
    }
    int status = _sendCANMessage(descriptor, messageId, messageData, dataParam.Length(), repeatPeriodMs);
    if (status < 0) {
        throwDeviceNotFoundError(env);
    }
    return Napi::Number::New(env, status);
}

// Params:
//   descriptor: string
//   messageId: Number
//   messageData: Number[]
//   repeatPeriod: Number
// Returns:
//   status: Number
Napi::Number sendCANMessageThroughHal(const Napi::CallbackInfo& info) {
Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t messageId = info[1].As<Napi::Number>().Uint32Value();
    Napi::Array dataParam = info[2].As<Napi::Array>();
    int repeatPeriodMs = info[3].As<Napi::Number>().Uint32Value();

    uint8_t messageData[8];
    for (uint32_t i = 0; i < dataParam.Length(); i++) {
        messageData[i] = dataParam.Get(i).As<Napi::Number>().Uint32Value();
    }

    int32_t status;
    HAL_CAN_SendMessage(messageId, messageData, dataParam.Length(), repeatPeriodMs, &status);
    return Napi::Number::New(env, (int)status);
}

// Params:
//   messageId: Number
//   messageData: Number[]
//   repeatPeriod: Number
// Returns:
//   status: Number
Napi::Number sendHALMessage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    uint32_t messageId = info[0].As<Napi::Number>().Uint32Value();
    Napi::Array dataParam = info[1].As<Napi::Array>();
    int repeatPeriodMs = info[2].As<Napi::Number>().Uint32Value();

    uint8_t messageData[8];
    for (uint32_t i = 0; i < dataParam.Length(); i++) {
        messageData[i] = dataParam.Get(i).As<Napi::Number>().Uint32Value();
    }

    int32_t status;
    HAL_CAN_SendMessage(messageId, messageData, dataParam.Length(), repeatPeriodMs, &status);
    return Napi::Number::New(env, (int)status);
}

// Params:
//   messageId: Number
//   messageMask: Number
//   numMessages: Number
// Returns:
//   streamHandle: Number
Napi::Number openHALStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    uint32_t messageId = info[0].As<Napi::Number>().Uint32Value();
    uint32_t messageMask = info[1].As<Napi::Number>().Uint32Value();
    uint32_t numMessages = info[2].As<Napi::Number>().Uint32Value();

    int32_t status;
    uint32_t streamHandle;
    HAL_CAN_OpenStreamSession(&streamHandle, messageId, messageMask, numMessages, &status);
    return Napi::Number::New(env, (int)streamHandle);
}

// Params:
//   streamHandle: Number
//   numMessages: Number
// Returns:
//   messages: Array
Napi::Array readHALStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    uint32_t streamHandle = info[0].As<Napi::Number>().Uint32Value();
    uint32_t numMessages = info[1].As<Napi::Number>().Uint32Value();

    int32_t status;
    uint32_t messagesRead;
    HAL_CANStreamMessage *messages = new HAL_CANStreamMessage[numMessages];
    HAL_CAN_ReadStreamSession(streamHandle, messages, numMessages, &messagesRead, &status);
    Napi::Array messageArray = Napi::Array::New(env);
    for (uint32_t i = 0; i < messagesRead && i < numMessages; i++) {
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
}

// Params:
//   streamHandle: Number
void closeHALStreamSession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    uint32_t streamHandle = info[0].As<Napi::Number>().Uint32Value();
    HAL_CAN_CloseStreamSession(streamHandle);
}

void initializeNotifier(const Napi::CallbackInfo& info) {
    int32_t status;
    m_notifier = HAL_InitializeNotifier(&status);
}

void waitForNotifierAlarm(const Napi::CallbackInfo& info) {
    uint32_t time = info[0].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[1].As<Napi::Function>();
    int32_t status;

    HAL_UpdateNotifierAlarm(m_notifier, HAL_GetFPGATime(&status) + time, &status);
    // TODO(Noah): Don't discard the returned value (this function is marked as [nodiscard])
    HAL_WaitForNotifierAlarm(m_notifier, &status);
    cb.Call(info.Env().Global(), {info.Env().Null(), Napi::Number::New(info.Env(), status)});
}

void stopNotifier(const Napi::CallbackInfo& info) {
    int32_t status;
    HAL_StopNotifier(m_notifier, &status);
    HAL_CleanNotifier(m_notifier, &status);
}

Napi::Promise writeDfuToBin(const Napi::CallbackInfo& info) {
    std::string dfuFileName = info[0].As<Napi::String>().Utf8Value();
    std::string binFileName = info[1].As<Napi::String>().Utf8Value();
    int elementIndex;

    if(info[2].IsUndefined() || info[2].IsNull()) {
        elementIndex = 0;
    } else {
        elementIndex = info[2].As<Napi::Number>().Int32Value();
    }

    dfuse::DFUFile dfuFile(dfuFileName.c_str());
    int status = 0;
    if (dfuFile && dfuFile.Images().size() > 0 && dfuFile.Images()[0]) {
        dfuFile.Images()[0].Write(binFileName, elementIndex, dfuse::writer::Bin);
    } else {
        status = 1;
    }
    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

    deferred.Resolve(Napi::Number::New(info.Env(), status));
    return deferred.Promise();
}

Napi::Array getImageElements(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string dfuFileName = info[0].As<Napi::String>().Utf8Value();
    const int imageIndex = info[1].As<Napi::Number>().Int32Value();

    Napi::Array elements = Napi::Array::New(env);

    const dfuse::DFUFile dfuFile(dfuFileName.c_str());

    if(imageIndex >= dfuFile.Images().size()) {
        const std::string errorMessage = "Image index out of range";
        Napi::Error::New(env, errorMessage).ThrowAsJavaScriptException();
        return elements;
    }

    const dfuse::DFUImage image = dfuFile.Images()[imageIndex];

    uint32_t elementsCount = 0;
    for(auto element: image.Elements()) {
        Napi::Object elementObject = Napi::Object::New(env);
        elementObject.Set("startAddress", element.Address());
        elementObject.Set("size", element.Size());

        elements[elementsCount++] = elementObject;
    }

    return elements;
}

Napi::Object getLatestMessageOfEveryReceivedArbId(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    uint32_t maxAgeMs = info[1].As<Napi::Number>().Uint32Value();

    std::shared_ptr<rev::usb::CANDevice> device;

    { // This block exists to define how long we hold canDevicesMtx
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) {
            if (devicesRegisteredToHal.find(descriptor) != devicesRegisteredToHal.end()) return receiveHalMessage(info);
            throwDeviceNotFoundError(env);
            return Napi::Object::New(env);
        }
        device = deviceIterator->second;
    }

    std::map<uint32_t, std::shared_ptr<rev::usb::CANMessage>> messages;
    bool success = device->CopyReceivedMessagesMap(messages);
    if (!success) {
        Napi::Error::New(env, "Failed to copy the map of received messages").ThrowAsJavaScriptException();
        return Napi::Object::New(env);
    }

     // TODO(Harper): Use HAL clock
    const auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch().count();

    Napi::Object result = Napi::Object::New(env);
    for (auto& m: messages) {
        uint32_t arbId = m.first;
        auto message = m.second;
        uint32_t timestampMs = message->GetTimestampUs();

        if (nowMs - timestampMs > maxAgeMs) {
            continue;
        }

        size_t messageSize = message->GetSize();
        const uint8_t* messageData = message->GetData();
        Napi::Array napiMessage = Napi::Array::New(env, messageSize);
        for (int i = 0; i < messageSize; i++) {
            napiMessage[i] =  messageData[i];
        }
        Napi::Object messageInfo = Napi::Object::New(env);
        messageInfo.Set("messageID", message->GetMessageId());
        messageInfo.Set("timeStamp", timestampMs);
        messageInfo.Set("data", napiMessage);
        result.Set(arbId, messageInfo);
    }

    return result;
}

void cleanupHeartbeatsRunning() {
    // Erase removed CAN buses from heartbeatsRunning
    std::scoped_lock lock{watchdogMtx, canDevicesMtx};
    for(int i = 0; i < heartbeatsRunning.size(); i++) {
        auto deviceIterator = canDeviceMap.find(heartbeatsRunning[i]);
        if (deviceIterator == canDeviceMap.end()) {
            heartbeatsRunning.erase(heartbeatsRunning.begin() + i);
        }
    }
}

void heartbeatsWatchdog() {
    while (true) {
        std::this_thread::sleep_for (std::chrono::milliseconds(250));

        cleanupHeartbeatsRunning();

        std::scoped_lock lock{watchdogMtx};

        if (heartbeatsRunning.size() < 1) { break; }

        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = now-latestHeartbeatAck;
        if (elapsed_seconds.count() >= 1 && !heartbeatTimeoutExpired) {
            // The heartbeat timeout just expired
            heartbeatTimeoutExpired = true;
            for(int i = 0; i < heartbeatsRunning.size(); i++) {
                if (sparkHeartbeatMap.contains(heartbeatsRunning[i])) {
                    // Clear the scheduled heartbeat that has outdated data so that the updated one gets sent out immediately
                    _sendCANMessage(heartbeatsRunning[i], SPARK_HEARTBEAT_ID, disabledSparkHeartbeat, SPARK_HEARTBEAT_LENGTH, -1);

                    _sendCANMessage(heartbeatsRunning[i], SPARK_HEARTBEAT_ID, disabledSparkHeartbeat, SPARK_HEARTBEAT_LENGTH, HEARTBEAT_PERIOD_MS);
                }
                if (revCommonHeartbeatMap.contains(heartbeatsRunning[i])) {
                    // Clear the scheduled heartbeat that has outdated data so that the updated one gets sent out immediately
                    _sendCANMessage(heartbeatsRunning[i], REV_COMMON_HEARTBEAT_ID, disabledRevCommonHeartbeat, REV_COMMON_HEARTBEAT_LENGTH, -1);

                    _sendCANMessage(heartbeatsRunning[i], REV_COMMON_HEARTBEAT_ID, disabledRevCommonHeartbeat, REV_COMMON_HEARTBEAT_LENGTH, HEARTBEAT_PERIOD_MS);
                }
            }
        } else if (elapsed_seconds.count() < 1 && heartbeatTimeoutExpired) {
            // The heartbeat timeout is newly un-expired
            heartbeatTimeoutExpired = false;
            for(int i = 0; i < heartbeatsRunning.size(); i++) {
                if (auto heartbeatEntry = sparkHeartbeatMap.find(heartbeatsRunning[i]); heartbeatEntry != sparkHeartbeatMap.end()) {
                    // Clear the scheduled heartbeat that has outdated data so that the updated one gets sent out immediately
                    _sendCANMessage(heartbeatsRunning[i], SPARK_HEARTBEAT_ID, heartbeatEntry->second.data(), SPARK_HEARTBEAT_LENGTH, -1);

                    _sendCANMessage(heartbeatsRunning[i], SPARK_HEARTBEAT_ID, heartbeatEntry->second.data(), SPARK_HEARTBEAT_LENGTH, HEARTBEAT_PERIOD_MS);
                }
                if (auto heartbeatEntry = revCommonHeartbeatMap.find(heartbeatsRunning[i]); heartbeatEntry != revCommonHeartbeatMap.end()) {
                    // Clear the scheduled heartbeat that has outdated data so that the updated one gets sent out immediately
                    _sendCANMessage(heartbeatsRunning[i], REV_COMMON_HEARTBEAT_ID, heartbeatEntry->second.data(), REV_COMMON_HEARTBEAT_LENGTH, -1);

                    _sendCANMessage(heartbeatsRunning[i], REV_COMMON_HEARTBEAT_ID, heartbeatEntry->second.data(), REV_COMMON_HEARTBEAT_LENGTH, HEARTBEAT_PERIOD_MS);
                }
            }
        }
    }
}

void ackHeartbeats(const Napi::CallbackInfo& info) {
    std::scoped_lock lock{watchdogMtx};
    latestHeartbeatAck = std::chrono::steady_clock::now();
}

// Params:
//   descriptor: string
void startRevCommonHeartbeat(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();

    {
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) return;
    }

    std::array<uint8_t, REV_COMMON_HEARTBEAT_LENGTH> payload = {1};

    std::scoped_lock lock{watchdogMtx};

    if (!heartbeatTimeoutExpired) {
        _sendCANMessage(descriptor, REV_COMMON_HEARTBEAT_ID, payload.data(), REV_COMMON_HEARTBEAT_LENGTH, HEARTBEAT_PERIOD_MS);
    }

    revCommonHeartbeatMap[descriptor] = payload;

    if (heartbeatsRunning.size() == 0) {
        heartbeatsRunning.push_back(descriptor);
        latestHeartbeatAck = std::chrono::steady_clock::now();
        std::thread hb(heartbeatsWatchdog);
        hb.detach();
    } else {
        for(int i = 0; i < heartbeatsRunning.size(); i++) {
            if (heartbeatsRunning[i].compare(descriptor) == 0) return;
        }
        heartbeatsRunning.push_back(descriptor);
    }
}

// Params:
//   descriptor: string
//   heartbeatData: Number[]
void setSparkMaxHeartbeatData(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    Napi::Array dataParam = info[1].As<Napi::Array>();

    std::array<uint8_t, SPARK_HEARTBEAT_LENGTH> heartbeat = {0, 0, 0, 0, 0, 0, 0, 0};

    {
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) return;
    }

    int sum = 0;
    for (uint32_t i = 0; i < dataParam.Length(); i++) {
        heartbeat[i] = dataParam.Get(i).As<Napi::Number>().Uint32Value();
        sum+= heartbeat[i];
    }

    std::scoped_lock lock{watchdogMtx};

    if (!heartbeatTimeoutExpired) {
        // Clear the scheduled heartbeat that has outdated data so that the updated one gets sent out immediately
        _sendCANMessage(descriptor, SPARK_HEARTBEAT_ID, heartbeat.data(), SPARK_HEARTBEAT_LENGTH, -1);

        _sendCANMessage(descriptor, SPARK_HEARTBEAT_ID, heartbeat.data(), SPARK_HEARTBEAT_LENGTH, HEARTBEAT_PERIOD_MS);
    }

    sparkHeartbeatMap[descriptor] = heartbeat;

    if (heartbeatsRunning.size() == 0) {
        heartbeatsRunning.push_back(descriptor);
        latestHeartbeatAck = std::chrono::steady_clock::now();
        std::thread hb(heartbeatsWatchdog);
        hb.detach();
    } else {
        for(int i = 0; i < heartbeatsRunning.size(); i++) {
            if (heartbeatsRunning[i].compare(descriptor) == 0) return;
        }
        heartbeatsRunning.push_back(descriptor);
    }
}

void stopHeartbeats(const Napi::CallbackInfo& info) {
    std::string descriptor = info[0].As<Napi::String>().Utf8Value();
    bool sendDisabledHeartbeatsFirst = info[1].As<Napi::Boolean>().Value();

    // 0 sends and then cancels, -1 cancels without sending
    const int repeatPeriod = sendDisabledHeartbeatsFirst ? 0 : -1;

    std::scoped_lock lock{watchdogMtx};
    // Send disabled SPARK and REV common heartbeats and un-schedule them for the future
    _sendCANMessage(descriptor, SPARK_HEARTBEAT_ID, disabledSparkHeartbeat, SPARK_HEARTBEAT_LENGTH, repeatPeriod);
    _sendCANMessage(descriptor, REV_COMMON_HEARTBEAT_ID, disabledRevCommonHeartbeat, REV_COMMON_HEARTBEAT_LENGTH, repeatPeriod);
}
