#include <rev/Drivers/CandleWinUSB/CandleWinUSBDevice.h>
#include <napi.h>
#include <typeinfo>
#include <iostream>

class NapiWinUSBDevice : public Napi::ObjectWrap<NapiWinUSBDevice> {
  public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    NapiWinUSBDevice(const Napi::CallbackInfo& info);
    Napi::Value GetName(const Napi::CallbackInfo& info);
    void SetDevice(std::unique_ptr<rev::usb::CANDevice>);

  private:
    std::unique_ptr<rev::usb::CANDevice> device;
};

Napi::Object NapiWinUSBDevice::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "NapiWinUSBDevice", {
        InstanceMethod("GetName", &NapiWinUSBDevice::GetName)
    });

    Napi::FunctionReference* constructor = new Napi::FunctionReference();
    *constructor = Napi::Persistent(func);
    exports.Set("NapiWinUSBDevice", func);

    return exports;
}

NapiWinUSBDevice::NapiWinUSBDevice(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NapiWinUSBDevice>(info) {
    Napi::Env env = info.Env();
    std::cout << "Creating device " << std::endl;
}

Napi::Value NapiWinUSBDevice::GetName(const Napi::CallbackInfo& info){
    Napi::Env env = info.Env();
    return Napi::String::New(env, this->device->GetName());
}
