#include <iostream>
#include <rev/CANBridge.h>
#include <rev/CANMessage.h>
#include <rev/CANStatus.h>
#include <rev/CANBridgeUtils.h>
#ifdef _WIN32
#include <rev/Drivers/CandleWinUSB/CandleWinUSBDriver.h>
#include <rev/Drivers/CandleWinUSB/CandleWinUSBDevice.h>
#else

#include "rev/Drivers/SerialPort/SerialDriver.h"
#endif


#include <utils/ThreadUtils.h>
#include <hal/HAL.h>
#include <hal/CAN.h>
#include <napi.h>
#include <thread>
#include <chrono>
#include <map>
#include <vector>
#include <set>
#include <exception>
#include <mutex>
#include <ctime>
#include "canWrapper.h"
#include "DfuSeFile.h"

#define DEVICE_NOT_FOUND_ERROR "Device not found.  Make sure to run getDevices()"

rev::usb::CANDriver* driver = 
#ifdef _WIN32
    new rev::usb::CandleWinUSBDriver();
#else
    new rev::usb::SerialDriver();
#endif

std::set<std::string> devicesRegisteredToHal; // TODO(Noah): Protect with mutex
bool halInitialized = false;
uint32_t m_notifier;

std::mutex canDevicesMtx;
std::map<std::string, std::shared_ptr<rev::usb::CANDevice>> canDeviceMap;

std::mutex watchdogMtx;
std::vector<std::string> heartbeatsRunning;
auto latestHeartbeatAck = std::chrono::system_clock::now();

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
            Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
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
    for (size_t i = 0; i < messageSize; i++) {
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
            Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0);
        }

        device = deviceIterator->second;
    }

    try {
        rev::usb::CANStatus status = device->OpenStreamSession(&sessionHandle, filter, maxSize);
        if (status != rev::usb::CANStatus::kOk) {
            Napi::Error::New(env, "Opening stream session failed with error code "+std::to_string((int)status)).ThrowAsJavaScriptException();
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
            Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
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
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
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
            Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
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
        Napi::Error::New(env, DEVICE_NOT_FOUND_ERROR).ThrowAsJavaScriptException();
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
    HAL_WaitForNotifierAlarm(m_notifier, &status);
    cb.Call(info.Env().Global(), {info.Env().Null(), Napi::Number::New(info.Env(), status)});
}

void stopNotifier(const Napi::CallbackInfo& info) {
    int32_t status;
    HAL_StopNotifier(m_notifier, &status);
    HAL_CleanNotifier(m_notifier, &status);
}

void writeDfuToBin(const Napi::CallbackInfo& info) {
    std::string dfuFileName = info[0].As<Napi::String>().Utf8Value();
    std::string binFileName = info[1].As<Napi::String>().Utf8Value();
    Napi::Function cb = info[2].As<Napi::Function>();

    dfuse::DFUFile dfuFile(dfuFileName.c_str());
    int status = 0;
    if (dfuFile && dfuFile.Images().size() > 0 && dfuFile.Images()[0]) {
        dfuFile.Images()[0].Write(binFileName, dfuse::writer::Bin);
    } else {
        status = 1;
    }
    cb.Call(info.Env().Global(), {info.Env().Null(), Napi::Number::New(info.Env(), status)});
}

void heartbeatsWatchdog() {
    while (true) {
        std::this_thread::sleep_for (std::chrono::seconds(1));

        {
            // Erase removed CAN buses from heartbeatsRunning
            std::scoped_lock lock{watchdogMtx, canDevicesMtx};
            for(size_t i = 0; i < heartbeatsRunning.size(); i++) {
                auto deviceIterator = canDeviceMap.find(heartbeatsRunning[i]);
                if (deviceIterator == canDeviceMap.end()) {
                    heartbeatsRunning.erase(heartbeatsRunning.begin() + i);
                }
            }
        }

        std::scoped_lock lock{watchdogMtx};

        if (heartbeatsRunning.size() < 1) { break; }

        auto now = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = now-latestHeartbeatAck;
        if (elapsed_seconds.count() > 1) {
            uint8_t sparkMaxHeartbeat[] = {0, 0, 0, 0, 0, 0, 0, 0};
            uint8_t revCommonHeartbeat[] = {0};
            for(size_t i = 0; i < heartbeatsRunning.size(); i++) {
                _sendCANMessage(heartbeatsRunning[i], 0x2052C80, sparkMaxHeartbeat, 8, -1);
                _sendCANMessage(heartbeatsRunning[i], 0x00502C0, revCommonHeartbeat, 1, -1);
            }
        }
    }
}

void ackHeartbeats(const Napi::CallbackInfo& info) {
    std::scoped_lock lock{watchdogMtx};
    latestHeartbeatAck = std::chrono::system_clock::now();
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

    uint8_t payload[] = {1};
    _sendCANMessage(descriptor, 0x00502C0, payload, 1, 20);

    std::scoped_lock lock{watchdogMtx};

    if (heartbeatsRunning.size() == 0) {
        heartbeatsRunning.push_back(descriptor);
        latestHeartbeatAck = std::chrono::system_clock::now();
        std::thread hb(heartbeatsWatchdog);
        hb.detach();
    } else {
        for(size_t i = 0; i < heartbeatsRunning.size(); i++) {
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

    uint8_t heartbeat[] = {0, 0, 0, 0, 0, 0, 0, 0};

    {
        std::scoped_lock lock{canDevicesMtx};
        auto deviceIterator = canDeviceMap.find(descriptor);
        if (deviceIterator == canDeviceMap.end()) return;
    }

    _sendCANMessage(descriptor, 0x2052C80, heartbeat, 8, -1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int sum = 0;
    for (uint32_t i = 0; i < dataParam.Length(); i++) {
        heartbeat[i] = dataParam.Get(i).As<Napi::Number>().Uint32Value();
        sum+= heartbeat[i];
    }

    if (sum == 0) {
        _sendCANMessage(descriptor, 0x2052C80, heartbeat, 8, -1);
    }
    else {
        _sendCANMessage(descriptor, 0x2052C80, heartbeat, 8, 10);

        std::scoped_lock lock{watchdogMtx};

        if (heartbeatsRunning.size() == 0) {
            heartbeatsRunning.push_back(descriptor);
            latestHeartbeatAck = std::chrono::system_clock::now();
            std::thread hb(heartbeatsWatchdog);
            hb.detach();
        } else {
            for(size_t i = 0; i < heartbeatsRunning.size(); i++) {
                if (heartbeatsRunning[i].compare(descriptor) == 0) return;
            }
            heartbeatsRunning.push_back(descriptor);
        }
    }
}

/**
 * This function was removed from commit b0ca096624286b1e975eaaa816e38599933b7e84, which broke MSVC builds.
 * It has been re-added here to fix the build.
 */
void stopHeartbeats(const Napi::CallbackInfo& info) {
    //! TODO: Reimplement this function with current codebase
    Napi::Env env = info.Env();
}