#pragma once
/**
 * @file IOModule.h
 * @brief Unified IO module with endpoint registry and scheduler.
 */

#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IODrivers/Ds18b20Driver.h"
#include "Modules/IOModule/IODrivers/GpioDriver.h"
#include "Modules/IOModule/IODrivers/Pcf8574Driver.h"
#include "Modules/IOModule/IOEndpoints/AnalogSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalActuatorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/Pcf8574MaskEndpoint.h"
#include "Modules/IOModule/IOEndpoints/RunningMedianAverageFloat.h"
#include "Modules/IOModule/IORegistry/IORegistry.h"
#include "Modules/IOModule/IOScheduler/IOScheduler.h"
#include "Modules/IOModule/IOModuleDataModel.h"
#include "Core/CommandRegistry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

struct IOModuleConfig {
    bool enabled = true;
    int32_t i2cSda = 21;
    int32_t i2cScl = 22;
    int32_t adsPollMs = 125;
    int32_t dsPollMs = 2000;
    uint8_t adsInternalAddr = 0x48;
    uint8_t adsExternalAddr = 0x49;
    int32_t adsGain = ADS1X15_GAIN_6144MV;
    int32_t adsRate = 1;
    bool pcfEnabled = true;
    uint8_t pcfAddress = 0x20;
    uint8_t pcfMaskDefault = 0;
    bool pcfActiveLow = true;
};

enum IOAnalogSource : uint8_t {
    IO_SRC_ADS_INTERNAL_SINGLE = 0,
    IO_SRC_ADS_EXTERNAL_DIFF = 1,
    IO_SRC_DS18_WATER = 2,
    IO_SRC_DS18_AIR = 3
};

typedef void (*IOAnalogValueCallback)(void* ctx, float value);

struct IOAnalogDefinition {
    char id[24] = {0};
    uint8_t source = IO_SRC_ADS_INTERNAL_SINGLE;
    uint8_t channel = 0;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
    float minValid = -32768.0f;
    float maxValid = 32767.0f;
    IOAnalogValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
};

struct IOAnalogSlotConfig {
    char name[24] = {0};
    uint8_t source = IO_SRC_ADS_INTERNAL_SINGLE;
    uint8_t channel = 0;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
    float minValid = -32768.0f;
    float maxValid = 32767.0f;
};

struct IODigitalOutputDefinition {
    char id[24] = {0};
    uint8_t pin = 0;
    bool activeHigh = false;
    bool initialOn = false;
    bool momentary = false;
    uint16_t pulseMs = 500;
};

struct IODigitalOutputSlotConfig {
    char name[24] = {0};
    uint8_t pin = 0;
    bool activeHigh = false;
    bool initialOn = false;
    bool momentary = false;
    int32_t pulseMs = 500;
};

struct IOLedMaskService {
    bool (*setMask)(void* ctx, uint8_t mask);
    bool (*turnOn)(void* ctx, uint8_t bit);
    bool (*turnOff)(void* ctx, uint8_t bit);
    bool (*getMask)(void* ctx, uint8_t* mask);
    void* ctx;
};

class IOModule : public Module {
public:
    const char* moduleId() const override { return "io"; }
    const char* taskName() const override { return "io"; }

    uint8_t dependencyCount() const override { return 3; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "datastore";
        if (i == 2) return "cmd";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

    void setOneWireBuses(OneWireBus* water, OneWireBus* air);
    bool defineAnalogInput(const IOAnalogDefinition& def);
    bool defineDigitalOutput(const IODigitalOutputDefinition& def);
    const char* analogSlotName(uint8_t idx) const;
    const char* endpointLabel(const char* endpointId) const;

    IORegistry& registry() { return registry_; }

private:
    static bool tickFastAds_(void* ctx, uint32_t nowMs);
    static bool tickSlowDs_(void* ctx, uint32_t nowMs);

    static bool svcSetMask_(void* ctx, uint8_t mask);
    static bool svcTurnOn_(void* ctx, uint8_t bit);
    static bool svcTurnOff_(void* ctx, uint8_t bit);
    static bool svcGetMask_(void* ctx, uint8_t* mask);

    bool setLedMask_(uint8_t mask, uint32_t tsMs);
    bool turnLedOn_(uint8_t bit, uint32_t tsMs);
    bool turnLedOff_(uint8_t bit, uint32_t tsMs);
    bool getLedMask_(uint8_t& mask) const;
    uint8_t pcfPhysicalFromLogical_(uint8_t logicalMask) const;
    uint8_t pcfLogicalFromPhysical_(uint8_t physicalMask) const;

    bool configureRuntime_();
    bool processAnalogDefinition_(uint8_t idx, uint32_t nowMs);
    bool endpointIndexFromId_(const char* id, uint8_t& idxOut) const;
    static bool writeDigitalOut_(void* ctx, bool on);
    static void digitalPulseTimerCb_(TimerHandle_t timer);
    static bool cmdIoWrite_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    bool handleIoWrite_(const CommandRequest& req, char* reply, size_t replyLen);

    static constexpr uint8_t MAX_ANALOG_ENDPOINTS = 12;
    static constexpr uint8_t MAX_DIGITAL_OUTPUTS = 12;
    static constexpr uint8_t ANALOG_CFG_SLOTS = 5;
    static constexpr uint8_t DIGITAL_CFG_SLOTS = 8;

    struct AnalogSlot {
        bool used = false;
        IOAnalogDefinition def{};
        AnalogSensorEndpoint* endpoint = nullptr;
        RunningMedianAverageFloat median{11, 5};
        bool lastRoundedValid = false;
        float lastRounded = 0.0f;
    };
    struct DigitalOutputSlot {
        bool used = false;
        IODigitalOutputDefinition def{};
        GpioDriver* driver = nullptr;
        DigitalActuatorEndpoint* endpoint = nullptr;
        TimerHandle_t pulseTimer = nullptr;
    };

    IOModuleConfig cfgData_{};
    IOAnalogSlotConfig analogCfg_[ANALOG_CFG_SLOTS]{};
    IODigitalOutputSlotConfig digitalCfg_[DIGITAL_CFG_SLOTS]{};

    const LogHubService* logHub_ = nullptr;
    ServiceRegistry* services_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;

    IORegistry registry_{};
    IOScheduler scheduler_{};
    I2CBus i2cBus_{};

    OneWireBus* oneWireWater_ = nullptr;
    OneWireBus* oneWireAir_ = nullptr;
    uint8_t oneWireWaterAddr_[8] = {0};
    uint8_t oneWireAirAddr_[8] = {0};

    Ads1115Driver* adsInternal_ = nullptr;
    Ads1115Driver* adsExternal_ = nullptr;
    Ds18b20Driver* dsWater_ = nullptr;
    Ds18b20Driver* dsAir_ = nullptr;

    Pcf8574Driver* pcf_ = nullptr;
    Pcf8574MaskEndpoint* ledMaskEp_ = nullptr;
    IOLedMaskService ledSvc_{ svcSetMask_, svcTurnOn_, svcTurnOff_, svcGetMask_, this };
    bool pcfLastEnabled_ = false;
    uint8_t pcfLogicalMask_ = 0;
    bool pcfLogicalValid_ = false;

    AnalogSlot analogSlots_[MAX_ANALOG_ENDPOINTS]{};
    DigitalOutputSlot digitalOutSlots_[MAX_DIGITAL_OUTPUTS]{};
    bool runtimeReady_ = false;

    ConfigVariable<bool,0> enabledVar_ { NVS_KEY("io_en"),"enabled","io",ConfigType::Bool,&cfgData_.enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSdaVar_ { NVS_KEY("io_sda"),"i2c_sda","io",ConfigType::Int32,&cfgData_.i2cSda,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSclVar_ { NVS_KEY("io_scl"),"i2c_scl","io",ConfigType::Int32,&cfgData_.i2cScl,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsPollVar_ { NVS_KEY("io_ads"),"ads_poll_ms","io",ConfigType::Int32,&cfgData_.adsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> dsPollVar_ { NVS_KEY("io_ds"),"ds_poll_ms","io",ConfigType::Int32,&cfgData_.dsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsInternalAddrVar_ { NVS_KEY("io_aiad"),"ads_internal_addr","io",ConfigType::UInt8,&cfgData_.adsInternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsExternalAddrVar_ { NVS_KEY("io_aead"),"ads_external_addr","io",ConfigType::UInt8,&cfgData_.adsExternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsGainVar_ { NVS_KEY("io_agai"),"ads_gain","io",ConfigType::Int32,&cfgData_.adsGain,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsRateVar_ { NVS_KEY("io_arat"),"ads_rate","io",ConfigType::Int32,&cfgData_.adsRate,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfEnabledVar_ { NVS_KEY("io_pcfen"),"pcf_enabled","io",ConfigType::Bool,&cfgData_.pcfEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfAddressVar_ { NVS_KEY("io_pcfad"),"pcf_address","io",ConfigType::UInt8,&cfgData_.pcfAddress,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfMaskDefaultVar_ { NVS_KEY("io_pcfmk"),"pcf_mask_default","io",ConfigType::UInt8,&cfgData_.pcfMaskDefault,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfActiveLowVar_ { NVS_KEY("io_pcfal"),"pcf_active_low","io",ConfigType::Bool,&cfgData_.pcfActiveLow,ConfigPersistence::Persistent,0 };

    ConfigVariable<char,0> a0NameVar_{NVS_KEY("io_a0nm"),"a0_name","io/input/a0",ConfigType::CharArray,(char*)analogCfg_[0].name,ConfigPersistence::Persistent,sizeof(analogCfg_[0].name)};
    ConfigVariable<uint8_t,0> a0SourceVar_{NVS_KEY("io_a0s"),"a0_source","io/input/a0",ConfigType::UInt8,&analogCfg_[0].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a0ChannelVar_{NVS_KEY("io_a0c"),"a0_channel","io/input/a0",ConfigType::UInt8,&analogCfg_[0].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0C0Var_{NVS_KEY("io_a00"),"a0_c0","io/input/a0",ConfigType::Float,&analogCfg_[0].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0C1Var_{NVS_KEY("io_a01"),"a0_c1","io/input/a0",ConfigType::Float,&analogCfg_[0].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a0PrecVar_{NVS_KEY("io_a0p"),"a0_prec","io/input/a0",ConfigType::Int32,&analogCfg_[0].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0MinVar_{NVS_KEY("io_a0n"),"a0_min","io/input/a0",ConfigType::Float,&analogCfg_[0].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0MaxVar_{NVS_KEY("io_a0x"),"a0_max","io/input/a0",ConfigType::Float,&analogCfg_[0].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a1NameVar_{NVS_KEY("io_a1nm"),"a1_name","io/input/a1",ConfigType::CharArray,(char*)analogCfg_[1].name,ConfigPersistence::Persistent,sizeof(analogCfg_[1].name)};
    ConfigVariable<uint8_t,0> a1SourceVar_{NVS_KEY("io_a1s"),"a1_source","io/input/a1",ConfigType::UInt8,&analogCfg_[1].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a1ChannelVar_{NVS_KEY("io_a1c"),"a1_channel","io/input/a1",ConfigType::UInt8,&analogCfg_[1].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1C0Var_{NVS_KEY("io_a10"),"a1_c0","io/input/a1",ConfigType::Float,&analogCfg_[1].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1C1Var_{NVS_KEY("io_a11"),"a1_c1","io/input/a1",ConfigType::Float,&analogCfg_[1].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a1PrecVar_{NVS_KEY("io_a1p"),"a1_prec","io/input/a1",ConfigType::Int32,&analogCfg_[1].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1MinVar_{NVS_KEY("io_a1n"),"a1_min","io/input/a1",ConfigType::Float,&analogCfg_[1].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1MaxVar_{NVS_KEY("io_a1x"),"a1_max","io/input/a1",ConfigType::Float,&analogCfg_[1].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a2NameVar_{NVS_KEY("io_a2nm"),"a2_name","io/input/a2",ConfigType::CharArray,(char*)analogCfg_[2].name,ConfigPersistence::Persistent,sizeof(analogCfg_[2].name)};
    ConfigVariable<uint8_t,0> a2SourceVar_{NVS_KEY("io_a2s"),"a2_source","io/input/a2",ConfigType::UInt8,&analogCfg_[2].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a2ChannelVar_{NVS_KEY("io_a2c"),"a2_channel","io/input/a2",ConfigType::UInt8,&analogCfg_[2].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2C0Var_{NVS_KEY("io_a20"),"a2_c0","io/input/a2",ConfigType::Float,&analogCfg_[2].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2C1Var_{NVS_KEY("io_a21"),"a2_c1","io/input/a2",ConfigType::Float,&analogCfg_[2].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a2PrecVar_{NVS_KEY("io_a2p"),"a2_prec","io/input/a2",ConfigType::Int32,&analogCfg_[2].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2MinVar_{NVS_KEY("io_a2n"),"a2_min","io/input/a2",ConfigType::Float,&analogCfg_[2].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2MaxVar_{NVS_KEY("io_a2x"),"a2_max","io/input/a2",ConfigType::Float,&analogCfg_[2].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a3NameVar_{NVS_KEY("io_a3nm"),"a3_name","io/input/a3",ConfigType::CharArray,(char*)analogCfg_[3].name,ConfigPersistence::Persistent,sizeof(analogCfg_[3].name)};
    ConfigVariable<uint8_t,0> a3SourceVar_{NVS_KEY("io_a3s"),"a3_source","io/input/a3",ConfigType::UInt8,&analogCfg_[3].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a3ChannelVar_{NVS_KEY("io_a3c"),"a3_channel","io/input/a3",ConfigType::UInt8,&analogCfg_[3].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3C0Var_{NVS_KEY("io_a30"),"a3_c0","io/input/a3",ConfigType::Float,&analogCfg_[3].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3C1Var_{NVS_KEY("io_a31"),"a3_c1","io/input/a3",ConfigType::Float,&analogCfg_[3].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a3PrecVar_{NVS_KEY("io_a3p"),"a3_prec","io/input/a3",ConfigType::Int32,&analogCfg_[3].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3MinVar_{NVS_KEY("io_a3n"),"a3_min","io/input/a3",ConfigType::Float,&analogCfg_[3].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3MaxVar_{NVS_KEY("io_a3x"),"a3_max","io/input/a3",ConfigType::Float,&analogCfg_[3].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a4NameVar_{NVS_KEY("io_a4nm"),"a4_name","io/input/a4",ConfigType::CharArray,(char*)analogCfg_[4].name,ConfigPersistence::Persistent,sizeof(analogCfg_[4].name)};
    ConfigVariable<uint8_t,0> a4SourceVar_{NVS_KEY("io_a4s"),"a4_source","io/input/a4",ConfigType::UInt8,&analogCfg_[4].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a4ChannelVar_{NVS_KEY("io_a4c"),"a4_channel","io/input/a4",ConfigType::UInt8,&analogCfg_[4].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4C0Var_{NVS_KEY("io_a40"),"a4_c0","io/input/a4",ConfigType::Float,&analogCfg_[4].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4C1Var_{NVS_KEY("io_a41"),"a4_c1","io/input/a4",ConfigType::Float,&analogCfg_[4].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a4PrecVar_{NVS_KEY("io_a4p"),"a4_prec","io/input/a4",ConfigType::Int32,&analogCfg_[4].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4MinVar_{NVS_KEY("io_a4n"),"a4_min","io/input/a4",ConfigType::Float,&analogCfg_[4].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4MaxVar_{NVS_KEY("io_a4x"),"a4_max","io/input/a4",ConfigType::Float,&analogCfg_[4].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d0NameVar_{NVS_KEY("io_d0nm"),"d0_name","io/output/d0",ConfigType::CharArray,(char*)digitalCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[0].name)};
    ConfigVariable<uint8_t,0> d0PinVar_{NVS_KEY("io_d0pn"),"d0_pin","io/output/d0",ConfigType::UInt8,&digitalCfg_[0].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0ActiveHighVar_{NVS_KEY("io_d0ah"),"d0_active_high","io/output/d0",ConfigType::Bool,&digitalCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0InitialOnVar_{NVS_KEY("io_d0in"),"d0_initial_on","io/output/d0",ConfigType::Bool,&digitalCfg_[0].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0MomentaryVar_{NVS_KEY("io_d0mo"),"d0_momentary","io/output/d0",ConfigType::Bool,&digitalCfg_[0].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d0PulseVar_{NVS_KEY("io_d0pm"),"d0_pulse_ms","io/output/d0",ConfigType::Int32,&digitalCfg_[0].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d1NameVar_{NVS_KEY("io_d1nm"),"d1_name","io/output/d1",ConfigType::CharArray,(char*)digitalCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[1].name)};
    ConfigVariable<uint8_t,0> d1PinVar_{NVS_KEY("io_d1pn"),"d1_pin","io/output/d1",ConfigType::UInt8,&digitalCfg_[1].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1ActiveHighVar_{NVS_KEY("io_d1ah"),"d1_active_high","io/output/d1",ConfigType::Bool,&digitalCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1InitialOnVar_{NVS_KEY("io_d1in"),"d1_initial_on","io/output/d1",ConfigType::Bool,&digitalCfg_[1].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1MomentaryVar_{NVS_KEY("io_d1mo"),"d1_momentary","io/output/d1",ConfigType::Bool,&digitalCfg_[1].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d1PulseVar_{NVS_KEY("io_d1pm"),"d1_pulse_ms","io/output/d1",ConfigType::Int32,&digitalCfg_[1].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d2NameVar_{NVS_KEY("io_d2nm"),"d2_name","io/output/d2",ConfigType::CharArray,(char*)digitalCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[2].name)};
    ConfigVariable<uint8_t,0> d2PinVar_{NVS_KEY("io_d2pn"),"d2_pin","io/output/d2",ConfigType::UInt8,&digitalCfg_[2].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2ActiveHighVar_{NVS_KEY("io_d2ah"),"d2_active_high","io/output/d2",ConfigType::Bool,&digitalCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2InitialOnVar_{NVS_KEY("io_d2in"),"d2_initial_on","io/output/d2",ConfigType::Bool,&digitalCfg_[2].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2MomentaryVar_{NVS_KEY("io_d2mo"),"d2_momentary","io/output/d2",ConfigType::Bool,&digitalCfg_[2].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d2PulseVar_{NVS_KEY("io_d2pm"),"d2_pulse_ms","io/output/d2",ConfigType::Int32,&digitalCfg_[2].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d3NameVar_{NVS_KEY("io_d3nm"),"d3_name","io/output/d3",ConfigType::CharArray,(char*)digitalCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[3].name)};
    ConfigVariable<uint8_t,0> d3PinVar_{NVS_KEY("io_d3pn"),"d3_pin","io/output/d3",ConfigType::UInt8,&digitalCfg_[3].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3ActiveHighVar_{NVS_KEY("io_d3ah"),"d3_active_high","io/output/d3",ConfigType::Bool,&digitalCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3InitialOnVar_{NVS_KEY("io_d3in"),"d3_initial_on","io/output/d3",ConfigType::Bool,&digitalCfg_[3].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3MomentaryVar_{NVS_KEY("io_d3mo"),"d3_momentary","io/output/d3",ConfigType::Bool,&digitalCfg_[3].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d3PulseVar_{NVS_KEY("io_d3pm"),"d3_pulse_ms","io/output/d3",ConfigType::Int32,&digitalCfg_[3].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d4NameVar_{NVS_KEY("io_d4nm"),"d4_name","io/output/d4",ConfigType::CharArray,(char*)digitalCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[4].name)};
    ConfigVariable<uint8_t,0> d4PinVar_{NVS_KEY("io_d4pn"),"d4_pin","io/output/d4",ConfigType::UInt8,&digitalCfg_[4].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4ActiveHighVar_{NVS_KEY("io_d4ah"),"d4_active_high","io/output/d4",ConfigType::Bool,&digitalCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4InitialOnVar_{NVS_KEY("io_d4in"),"d4_initial_on","io/output/d4",ConfigType::Bool,&digitalCfg_[4].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4MomentaryVar_{NVS_KEY("io_d4mo"),"d4_momentary","io/output/d4",ConfigType::Bool,&digitalCfg_[4].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d4PulseVar_{NVS_KEY("io_d4pm"),"d4_pulse_ms","io/output/d4",ConfigType::Int32,&digitalCfg_[4].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d5NameVar_{NVS_KEY("io_d5nm"),"d5_name","io/output/d5",ConfigType::CharArray,(char*)digitalCfg_[5].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[5].name)};
    ConfigVariable<uint8_t,0> d5PinVar_{NVS_KEY("io_d5pn"),"d5_pin","io/output/d5",ConfigType::UInt8,&digitalCfg_[5].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5ActiveHighVar_{NVS_KEY("io_d5ah"),"d5_active_high","io/output/d5",ConfigType::Bool,&digitalCfg_[5].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5InitialOnVar_{NVS_KEY("io_d5in"),"d5_initial_on","io/output/d5",ConfigType::Bool,&digitalCfg_[5].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5MomentaryVar_{NVS_KEY("io_d5mo"),"d5_momentary","io/output/d5",ConfigType::Bool,&digitalCfg_[5].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d5PulseVar_{NVS_KEY("io_d5pm"),"d5_pulse_ms","io/output/d5",ConfigType::Int32,&digitalCfg_[5].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d6NameVar_{NVS_KEY("io_d6nm"),"d6_name","io/output/d6",ConfigType::CharArray,(char*)digitalCfg_[6].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[6].name)};
    ConfigVariable<uint8_t,0> d6PinVar_{NVS_KEY("io_d6pn"),"d6_pin","io/output/d6",ConfigType::UInt8,&digitalCfg_[6].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6ActiveHighVar_{NVS_KEY("io_d6ah"),"d6_active_high","io/output/d6",ConfigType::Bool,&digitalCfg_[6].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6InitialOnVar_{NVS_KEY("io_d6in"),"d6_initial_on","io/output/d6",ConfigType::Bool,&digitalCfg_[6].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6MomentaryVar_{NVS_KEY("io_d6mo"),"d6_momentary","io/output/d6",ConfigType::Bool,&digitalCfg_[6].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d6PulseVar_{NVS_KEY("io_d6pm"),"d6_pulse_ms","io/output/d6",ConfigType::Int32,&digitalCfg_[6].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d7NameVar_{NVS_KEY("io_d7nm"),"d7_name","io/output/d7",ConfigType::CharArray,(char*)digitalCfg_[7].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[7].name)};
    ConfigVariable<uint8_t,0> d7PinVar_{NVS_KEY("io_d7pn"),"d7_pin","io/output/d7",ConfigType::UInt8,&digitalCfg_[7].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7ActiveHighVar_{NVS_KEY("io_d7ah"),"d7_active_high","io/output/d7",ConfigType::Bool,&digitalCfg_[7].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7InitialOnVar_{NVS_KEY("io_d7in"),"d7_initial_on","io/output/d7",ConfigType::Bool,&digitalCfg_[7].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7MomentaryVar_{NVS_KEY("io_d7mo"),"d7_momentary","io/output/d7",ConfigType::Bool,&digitalCfg_[7].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d7PulseVar_{NVS_KEY("io_d7pm"),"d7_pulse_ms","io/output/d7",ConfigType::Int32,&digitalCfg_[7].pulseMs,ConfigPersistence::Persistent,0};
};
