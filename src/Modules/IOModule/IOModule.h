#pragma once
/**
 * @file IOModule.h
 * @brief Unified IO module with endpoint registry and scheduler.
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/Layout/PoolIoMap.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/Services/Services.h"
#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IODrivers/Ds18b20Driver.h"
#include "Modules/IOModule/IODrivers/GpioDriver.h"
#include "Modules/IOModule/IODrivers/Pcf8574Driver.h"
#include "Modules/IOModule/IOEndpoints/AnalogSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalActuatorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/Pcf8574MaskEndpoint.h"
#include "Modules/IOModule/IOEndpoints/RunningMedianAverageFloat.h"
#include "Modules/IOModule/IORegistry/IORegistry.h"
#include "Modules/IOModule/IOScheduler/IOScheduler.h"
#include "Modules/IOModule/IOModuleDataModel.h"

struct IOModuleConfig {
    bool enabled = true;
    int32_t i2cSda = 21;
    int32_t i2cScl = 22;
    int32_t adsPollMs = 125;
    int32_t dsPollMs = 2000;
    int32_t digitalPollMs = 100;
    uint8_t adsInternalAddr = 0x48;
    uint8_t adsExternalAddr = 0x49;
    int32_t adsGain = ADS1X15_GAIN_6144MV;
    int32_t adsRate = 1;
    bool pcfEnabled = true;
    uint8_t pcfAddress = 0x20;
    uint8_t pcfMaskDefault = 0;
    bool pcfActiveLow = true;
    bool traceEnabled = true;
    int32_t tracePeriodMs = 5000;
};

enum IOAnalogSource : uint8_t {
    IO_SRC_ADS_INTERNAL_SINGLE = 0,
    IO_SRC_ADS_EXTERNAL_DIFF = 1,
    IO_SRC_DS18_WATER = 2,
    IO_SRC_DS18_AIR = 3
};

typedef void (*IOAnalogValueCallback)(void* ctx, float value);
typedef void (*IODigitalValueCallback)(void* ctx, bool value);

enum IODigitalPullMode : uint8_t {
    IO_PULL_NONE = 0,
    IO_PULL_UP = 1,
    IO_PULL_DOWN = 2
};

struct IOAnalogDefinition {
    char id[24] = {0};
    /** Required explicit AI id in [IO_ID_AI_BASE..IO_ID_AI_BASE+MAX_ANALOG_ENDPOINTS). */
    IoId ioId = IO_ID_INVALID;
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
    /** Required explicit DO id in [IO_ID_DO_BASE..IO_ID_DO_BASE+MAX_DIGITAL_OUTPUTS). */
    IoId ioId = IO_ID_INVALID;
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

struct IODigitalInputDefinition {
    char id[24] = {0};
    /** Required explicit DI id in [IO_ID_DI_BASE..IO_ID_DI_BASE+MAX_DIGITAL_INPUTS). */
    IoId ioId = IO_ID_INVALID;
    uint8_t pin = 0;
    bool activeHigh = true;
    uint8_t pullMode = IO_PULL_NONE;
    IODigitalValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
};

class IOModule : public Module, public IRuntimeSnapshotProvider {
public:
    const char* moduleId() const override { return "io"; }
    const char* taskName() const override { return "io"; }

    uint8_t dependencyCount() const override { return 4; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "datastore";
        if (i == 2) return "mqtt";
        if (i == 3) return "ha";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

    void setOneWireBuses(OneWireBus* water, OneWireBus* air);
    bool defineAnalogInput(const IOAnalogDefinition& def);
    bool defineDigitalInput(const IODigitalInputDefinition& def);
    bool defineDigitalOutput(const IODigitalOutputDefinition& def);
    const char* analogSlotName(uint8_t idx) const;
    const char* endpointLabel(const char* endpointId) const;
    bool buildInputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildOutputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;

    IORegistry& registry() { return registry_; }

private:
    static bool tickFastAds_(void* ctx, uint32_t nowMs);
    static bool tickSlowDs_(void* ctx, uint32_t nowMs);
    static bool tickDigitalInputs_(void* ctx, uint32_t nowMs);

    static uint8_t svcCount_(void* ctx);
    static IoStatus svcIdAt_(void* ctx, uint8_t index, IoId* outId);
    static IoStatus svcMeta_(void* ctx, IoId id, IoEndpointMeta* outMeta);
    static IoStatus svcReadDigital_(void* ctx, IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq);
    static IoStatus svcWriteDigital_(void* ctx, IoId id, uint8_t on, uint32_t tsMs);
    static IoStatus svcReadAnalog_(void* ctx, IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq);
    static IoStatus svcTick_(void* ctx, uint32_t nowMs);
    static IoStatus svcLastCycle_(void* ctx, IoCycleInfo* outCycle);

    static bool svcSetMask_(void* ctx, uint8_t mask);
    static bool svcTurnOn_(void* ctx, uint8_t bit);
    static bool svcTurnOff_(void* ctx, uint8_t bit);
    static bool svcGetMask_(void* ctx, uint8_t* mask);

    uint8_t ioCount_() const;
    IoStatus ioIdAt_(uint8_t index, IoId* outId) const;
    IoStatus ioMeta_(IoId id, IoEndpointMeta* outMeta) const;
    IoStatus ioReadDigital_(IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioWriteDigital_(IoId id, uint8_t on, uint32_t tsMs);
    IoStatus ioReadAnalog_(IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioTick_(uint32_t nowMs);
    IoStatus ioLastCycle_(IoCycleInfo* outCycle) const;

    bool setLedMask_(uint8_t mask, uint32_t tsMs);
    bool turnLedOn_(uint8_t bit, uint32_t tsMs);
    bool turnLedOff_(uint8_t bit, uint32_t tsMs);
    bool getLedMask_(uint8_t& mask) const;
    uint8_t pcfPhysicalFromLogical_(uint8_t logicalMask) const;
    uint8_t pcfLogicalFromPhysical_(uint8_t physicalMask) const;

    bool configureRuntime_();
    bool runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& routeTypeOut, uint8_t& slotIdxOut) const;
    bool buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const;
    bool processAnalogDefinition_(uint8_t idx, uint32_t nowMs);
    bool processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs);
    int32_t clampPrecisionForHa_(int32_t precision) const;
    void buildHaValueTemplate_(uint8_t analogIdx, char* out, size_t outLen) const;
    void registerHaAnalogSensors_();
    void forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs);
    void maybeRefreshHaOnPrecisionChange_();
    bool endpointIndexFromId_(const char* id, uint8_t& idxOut) const;
    bool digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const;
    bool findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const;
    bool findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const;
    void beginIoCycle_(uint32_t nowMs);
    void markIoCycleChanged_(IoId id);
    static bool writeDigitalOut_(void* ctx, bool on);
    void pollPulseOutputs_(uint32_t nowMs);
    AnalogSensorEndpoint* allocAnalogEndpoint_(const char* endpointId);
    DigitalSensorEndpoint* allocDigitalSensorEndpoint_(const char* endpointId);
    DigitalActuatorEndpoint* allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx);
    IDigitalPinDriver* allocGpioDriver_(const char* driverId, uint8_t pin, bool output, bool activeHigh, uint8_t inputPullMode = GpioDriver::PullNone);
    IAnalogSourceDriver* allocAdsDriver_(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg);
    IAnalogSourceDriver* allocDsDriver_(const char* driverId, OneWireBus* bus, const uint8_t address[8], const Ds18b20DriverConfig& cfg);
    IMaskOutputDriver* allocPcfDriver_(const char* driverId, I2CBus* bus, uint8_t address);
    Pcf8574MaskEndpoint* allocMaskEndpoint_(const char* endpointId, MaskWriteFn writeFn, MaskReadFn readFn, void* fnCtx);

    static constexpr uint8_t MAX_ANALOG_ENDPOINTS = 12;
    static constexpr uint8_t MAX_DIGITAL_INPUTS = 8;
    static constexpr uint8_t MAX_DIGITAL_OUTPUTS = 12;
    static constexpr uint8_t MAX_DIGITAL_SLOTS = MAX_DIGITAL_INPUTS + MAX_DIGITAL_OUTPUTS;
    static constexpr uint8_t ANALOG_CFG_SLOTS = 6;
    static constexpr uint8_t DIGITAL_CFG_SLOTS = 8;
    /** End-exclusive upper bounds for each static id range. */
    static constexpr IoId IO_ID_DO_MAX = IO_ID_DO_BASE + MAX_DIGITAL_OUTPUTS;
    static constexpr IoId IO_ID_DI_MAX = IO_ID_DI_BASE + MAX_DIGITAL_INPUTS;
    static constexpr IoId IO_ID_AI_MAX = IO_ID_AI_BASE + MAX_ANALOG_ENDPOINTS;

    struct AnalogSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOAnalogDefinition def{};
        AnalogSensorEndpoint* endpoint = nullptr;
        RunningMedianAverageFloat median{11, 5};
        bool lastAdsSampleSeqValid = false;
        uint32_t lastAdsSampleSeq = 0;
        bool lastRoundedValid = false;
        float lastRounded = 0.0f;
    };
    enum DigitalSlotKind : uint8_t {
        DIGITAL_SLOT_INPUT = 0,
        DIGITAL_SLOT_OUTPUT = 1
    };
    struct DigitalSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOModule* owner = nullptr;
        uint8_t kind = DIGITAL_SLOT_INPUT;
        uint8_t logicalIdx = 0;
        char endpointId[8] = {0};
        IODigitalInputDefinition inDef{};
        IODigitalOutputDefinition outDef{};
        IDigitalPinDriver* driver = nullptr;
        IOEndpoint* endpoint = nullptr;
        bool pulseArmed = false;
        uint32_t pulseDeadlineMs = 0;
        bool lastValid = false;
        bool lastValue = false;
    };

    IOModuleConfig cfgData_{};
    IOAnalogSlotConfig analogCfg_[ANALOG_CFG_SLOTS]{};
    IODigitalOutputSlotConfig digitalCfg_[DIGITAL_CFG_SLOTS]{};

    const LogHubService* logHub_ = nullptr;
    const HAService* haSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;

    IORegistry registry_{};
    IOScheduler scheduler_{};
    I2CBus i2cBus_{};

    OneWireBus* oneWireWater_ = nullptr;
    OneWireBus* oneWireAir_ = nullptr;
    uint8_t oneWireWaterAddr_[8] = {0};
    uint8_t oneWireAirAddr_[8] = {0};

    IAnalogSourceDriver* adsInternal_ = nullptr;
    IAnalogSourceDriver* adsExternal_ = nullptr;
    IAnalogSourceDriver* dsWater_ = nullptr;
    IAnalogSourceDriver* dsAir_ = nullptr;

    IMaskOutputDriver* pcf_ = nullptr;
    Pcf8574MaskEndpoint* ledMaskEp_ = nullptr;
    IOServiceV2 ioSvc_{
        svcCount_,
        svcIdAt_,
        svcMeta_,
        svcReadDigital_,
        svcWriteDigital_,
        svcReadAnalog_,
        svcTick_,
        svcLastCycle_,
        this
    };
    bool pcfLastEnabled_ = false;
    uint8_t pcfLogicalMask_ = 0;
    bool pcfLogicalValid_ = false;
    IoCycleInfo lastCycle_{};

    AnalogSlot analogSlots_[MAX_ANALOG_ENDPOINTS]{};
    DigitalSlot digitalSlots_[MAX_DIGITAL_SLOTS]{};
    alignas(AnalogSensorEndpoint) uint8_t analogEndpointPool_[MAX_ANALOG_ENDPOINTS][sizeof(AnalogSensorEndpoint)]{};
    alignas(DigitalSensorEndpoint) uint8_t digitalSensorEndpointPool_[MAX_DIGITAL_INPUTS][sizeof(DigitalSensorEndpoint)]{};
    alignas(DigitalActuatorEndpoint) uint8_t digitalActuatorEndpointPool_[MAX_DIGITAL_OUTPUTS][sizeof(DigitalActuatorEndpoint)]{};
    alignas(GpioDriver) uint8_t gpioDriverPool_[MAX_DIGITAL_SLOTS][sizeof(GpioDriver)]{};
    alignas(Ads1115Driver) uint8_t adsDriverPool_[2][sizeof(Ads1115Driver)]{};
    alignas(Ds18b20Driver) uint8_t dsDriverPool_[2][sizeof(Ds18b20Driver)]{};
    alignas(Pcf8574Driver) uint8_t pcfDriverPool_[1][sizeof(Pcf8574Driver)]{};
    alignas(Pcf8574MaskEndpoint) uint8_t maskEndpointPool_[1][sizeof(Pcf8574MaskEndpoint)]{};
    uint8_t analogEndpointPoolUsed_ = 0;
    uint8_t digitalSensorEndpointPoolUsed_ = 0;
    uint8_t digitalActuatorEndpointPoolUsed_ = 0;
    uint8_t gpioDriverPoolUsed_ = 0;
    uint8_t adsDriverPoolUsed_ = 0;
    uint8_t dsDriverPoolUsed_ = 0;
    uint8_t pcfDriverPoolUsed_ = 0;
    uint8_t maskEndpointPoolUsed_ = 0;
    bool runtimeReady_ = false;
    bool runtimeInitAttempted_ = false;
    bool pcfEnableNeedsReinitWarned_ = false;
    uint32_t analogCalcLogLastMs_[3]{0, 0, 0};
    int32_t haPrecisionLast_[ANALOG_CFG_SLOTS]{0, 0, 0, 0, 0, 0};
    bool haPrecisionLastInit_ = false;
    char haValueTpl_[ANALOG_CFG_SLOTS][64]{};
    char haSwitchStateSuffix_[FLOW_POOL_IO_BINDING_COUNT][24]{};
    char haSwitchPayloadOn_[FLOW_POOL_IO_BINDING_COUNT][64]{};
    char haSwitchPayloadOff_[FLOW_POOL_IO_BINDING_COUNT][64]{};

    ConfigVariable<bool,0> enabledVar_ { NVS_KEY(NvsKeys::Io::IO_EN),"enabled","io",ConfigType::Bool,&cfgData_.enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSdaVar_ { NVS_KEY(NvsKeys::Io::IO_SDA),"i2c_sda","io",ConfigType::Int32,&cfgData_.i2cSda,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSclVar_ { NVS_KEY(NvsKeys::Io::IO_SCL),"i2c_scl","io",ConfigType::Int32,&cfgData_.i2cScl,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsPollVar_ { NVS_KEY(NvsKeys::Io::IO_ADS),"ads_poll_ms","io",ConfigType::Int32,&cfgData_.adsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> dsPollVar_ { NVS_KEY(NvsKeys::Io::IO_DS),"ds_poll_ms","io",ConfigType::Int32,&cfgData_.dsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> digitalPollVar_ { NVS_KEY(NvsKeys::Io::IO_DIN),"digital_poll_ms","io",ConfigType::Int32,&cfgData_.digitalPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsInternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AIAD),"ads_internal_addr","io",ConfigType::UInt8,&cfgData_.adsInternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsExternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AEAD),"ads_external_addr","io",ConfigType::UInt8,&cfgData_.adsExternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsGainVar_ { NVS_KEY(NvsKeys::Io::IO_AGAI),"ads_gain","io",ConfigType::Int32,&cfgData_.adsGain,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsRateVar_ { NVS_KEY(NvsKeys::Io::IO_ARAT),"ads_rate","io",ConfigType::Int32,&cfgData_.adsRate,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_PCFEN),"pcf_enabled","io",ConfigType::Bool,&cfgData_.pcfEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfAddressVar_ { NVS_KEY(NvsKeys::Io::IO_PCFAD),"pcf_address","io",ConfigType::UInt8,&cfgData_.pcfAddress,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfMaskDefaultVar_ { NVS_KEY(NvsKeys::Io::IO_PCFMK),"pcf_mask_default","io",ConfigType::UInt8,&cfgData_.pcfMaskDefault,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfActiveLowVar_ { NVS_KEY(NvsKeys::Io::IO_PCFAL),"pcf_active_low","io",ConfigType::Bool,&cfgData_.pcfActiveLow,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> traceEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_TREN),"trace_enabled","io/debug",ConfigType::Bool,&cfgData_.traceEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> tracePeriodVar_ { NVS_KEY(NvsKeys::Io::IO_TRMS),"trace_period_ms","io/debug",ConfigType::Int32,&cfgData_.tracePeriodMs,ConfigPersistence::Persistent,0 };

    ConfigVariable<char,0> a0NameVar_{NVS_KEY(NvsKeys::Io::IO_A0NM),"a0_name","io/input/a0",ConfigType::CharArray,(char*)analogCfg_[0].name,ConfigPersistence::Persistent,sizeof(analogCfg_[0].name)};
    ConfigVariable<uint8_t,0> a0SourceVar_{NVS_KEY(NvsKeys::Io::IO_A0S),"a0_source","io/input/a0",ConfigType::UInt8,&analogCfg_[0].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a0ChannelVar_{NVS_KEY(NvsKeys::Io::IO_A0C),"a0_channel","io/input/a0",ConfigType::UInt8,&analogCfg_[0].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0C0Var_{NVS_KEY(NvsKeys::Io::IO_A00),"a0_c0","io/input/a0",ConfigType::Float,&analogCfg_[0].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0C1Var_{NVS_KEY(NvsKeys::Io::IO_A01),"a0_c1","io/input/a0",ConfigType::Float,&analogCfg_[0].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a0PrecVar_{NVS_KEY(NvsKeys::Io::IO_A0P),"a0_prec","io/input/a0",ConfigType::Int32,&analogCfg_[0].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0MinVar_{NVS_KEY(NvsKeys::Io::IO_A0N),"a0_min","io/input/a0",ConfigType::Float,&analogCfg_[0].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a0MaxVar_{NVS_KEY(NvsKeys::Io::IO_A0X),"a0_max","io/input/a0",ConfigType::Float,&analogCfg_[0].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a1NameVar_{NVS_KEY(NvsKeys::Io::IO_A1NM),"a1_name","io/input/a1",ConfigType::CharArray,(char*)analogCfg_[1].name,ConfigPersistence::Persistent,sizeof(analogCfg_[1].name)};
    ConfigVariable<uint8_t,0> a1SourceVar_{NVS_KEY(NvsKeys::Io::IO_A1S),"a1_source","io/input/a1",ConfigType::UInt8,&analogCfg_[1].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a1ChannelVar_{NVS_KEY(NvsKeys::Io::IO_A1C),"a1_channel","io/input/a1",ConfigType::UInt8,&analogCfg_[1].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1C0Var_{NVS_KEY(NvsKeys::Io::IO_A10),"a1_c0","io/input/a1",ConfigType::Float,&analogCfg_[1].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1C1Var_{NVS_KEY(NvsKeys::Io::IO_A11),"a1_c1","io/input/a1",ConfigType::Float,&analogCfg_[1].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a1PrecVar_{NVS_KEY(NvsKeys::Io::IO_A1P),"a1_prec","io/input/a1",ConfigType::Int32,&analogCfg_[1].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1MinVar_{NVS_KEY(NvsKeys::Io::IO_A1N),"a1_min","io/input/a1",ConfigType::Float,&analogCfg_[1].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a1MaxVar_{NVS_KEY(NvsKeys::Io::IO_A1X),"a1_max","io/input/a1",ConfigType::Float,&analogCfg_[1].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a2NameVar_{NVS_KEY(NvsKeys::Io::IO_A2NM),"a2_name","io/input/a2",ConfigType::CharArray,(char*)analogCfg_[2].name,ConfigPersistence::Persistent,sizeof(analogCfg_[2].name)};
    ConfigVariable<uint8_t,0> a2SourceVar_{NVS_KEY(NvsKeys::Io::IO_A2S),"a2_source","io/input/a2",ConfigType::UInt8,&analogCfg_[2].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a2ChannelVar_{NVS_KEY(NvsKeys::Io::IO_A2C),"a2_channel","io/input/a2",ConfigType::UInt8,&analogCfg_[2].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2C0Var_{NVS_KEY(NvsKeys::Io::IO_A20),"a2_c0","io/input/a2",ConfigType::Float,&analogCfg_[2].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2C1Var_{NVS_KEY(NvsKeys::Io::IO_A21),"a2_c1","io/input/a2",ConfigType::Float,&analogCfg_[2].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a2PrecVar_{NVS_KEY(NvsKeys::Io::IO_A2P),"a2_prec","io/input/a2",ConfigType::Int32,&analogCfg_[2].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2MinVar_{NVS_KEY(NvsKeys::Io::IO_A2N),"a2_min","io/input/a2",ConfigType::Float,&analogCfg_[2].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a2MaxVar_{NVS_KEY(NvsKeys::Io::IO_A2X),"a2_max","io/input/a2",ConfigType::Float,&analogCfg_[2].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a3NameVar_{NVS_KEY(NvsKeys::Io::IO_A3NM),"a3_name","io/input/a3",ConfigType::CharArray,(char*)analogCfg_[3].name,ConfigPersistence::Persistent,sizeof(analogCfg_[3].name)};
    ConfigVariable<uint8_t,0> a3SourceVar_{NVS_KEY(NvsKeys::Io::IO_A3S),"a3_source","io/input/a3",ConfigType::UInt8,&analogCfg_[3].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a3ChannelVar_{NVS_KEY(NvsKeys::Io::IO_A3C),"a3_channel","io/input/a3",ConfigType::UInt8,&analogCfg_[3].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3C0Var_{NVS_KEY(NvsKeys::Io::IO_A30),"a3_c0","io/input/a3",ConfigType::Float,&analogCfg_[3].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3C1Var_{NVS_KEY(NvsKeys::Io::IO_A31),"a3_c1","io/input/a3",ConfigType::Float,&analogCfg_[3].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a3PrecVar_{NVS_KEY(NvsKeys::Io::IO_A3P),"a3_prec","io/input/a3",ConfigType::Int32,&analogCfg_[3].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3MinVar_{NVS_KEY(NvsKeys::Io::IO_A3N),"a3_min","io/input/a3",ConfigType::Float,&analogCfg_[3].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a3MaxVar_{NVS_KEY(NvsKeys::Io::IO_A3X),"a3_max","io/input/a3",ConfigType::Float,&analogCfg_[3].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a4NameVar_{NVS_KEY(NvsKeys::Io::IO_A4NM),"a4_name","io/input/a4",ConfigType::CharArray,(char*)analogCfg_[4].name,ConfigPersistence::Persistent,sizeof(analogCfg_[4].name)};
    ConfigVariable<uint8_t,0> a4SourceVar_{NVS_KEY(NvsKeys::Io::IO_A4S),"a4_source","io/input/a4",ConfigType::UInt8,&analogCfg_[4].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a4ChannelVar_{NVS_KEY(NvsKeys::Io::IO_A4C),"a4_channel","io/input/a4",ConfigType::UInt8,&analogCfg_[4].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4C0Var_{NVS_KEY(NvsKeys::Io::IO_A40),"a4_c0","io/input/a4",ConfigType::Float,&analogCfg_[4].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4C1Var_{NVS_KEY(NvsKeys::Io::IO_A41),"a4_c1","io/input/a4",ConfigType::Float,&analogCfg_[4].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a4PrecVar_{NVS_KEY(NvsKeys::Io::IO_A4P),"a4_prec","io/input/a4",ConfigType::Int32,&analogCfg_[4].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4MinVar_{NVS_KEY(NvsKeys::Io::IO_A4N),"a4_min","io/input/a4",ConfigType::Float,&analogCfg_[4].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a4MaxVar_{NVS_KEY(NvsKeys::Io::IO_A4X),"a4_max","io/input/a4",ConfigType::Float,&analogCfg_[4].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> a5NameVar_{NVS_KEY(NvsKeys::Io::IO_A5NM),"a5_name","io/input/a5",ConfigType::CharArray,(char*)analogCfg_[5].name,ConfigPersistence::Persistent,sizeof(analogCfg_[5].name)};
    ConfigVariable<uint8_t,0> a5SourceVar_{NVS_KEY(NvsKeys::Io::IO_A5S),"a5_source","io/input/a5",ConfigType::UInt8,&analogCfg_[5].source,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> a5ChannelVar_{NVS_KEY(NvsKeys::Io::IO_A5C),"a5_channel","io/input/a5",ConfigType::UInt8,&analogCfg_[5].channel,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a5C0Var_{NVS_KEY(NvsKeys::Io::IO_A50),"a5_c0","io/input/a5",ConfigType::Float,&analogCfg_[5].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a5C1Var_{NVS_KEY(NvsKeys::Io::IO_A51),"a5_c1","io/input/a5",ConfigType::Float,&analogCfg_[5].c1,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> a5PrecVar_{NVS_KEY(NvsKeys::Io::IO_A5P),"a5_prec","io/input/a5",ConfigType::Int32,&analogCfg_[5].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a5MinVar_{NVS_KEY(NvsKeys::Io::IO_A5N),"a5_min","io/input/a5",ConfigType::Float,&analogCfg_[5].minValid,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> a5MaxVar_{NVS_KEY(NvsKeys::Io::IO_A5X),"a5_max","io/input/a5",ConfigType::Float,&analogCfg_[5].maxValid,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d0NameVar_{NVS_KEY(NvsKeys::Io::IO_D0NM),"d0_name","io/output/d0",ConfigType::CharArray,(char*)digitalCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[0].name)};
    ConfigVariable<uint8_t,0> d0PinVar_{NVS_KEY(NvsKeys::Io::IO_D0PN),"d0_pin","io/output/d0",ConfigType::UInt8,&digitalCfg_[0].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D0AH),"d0_active_high","io/output/d0",ConfigType::Bool,&digitalCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D0IN),"d0_initial_on","io/output/d0",ConfigType::Bool,&digitalCfg_[0].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D0MO),"d0_momentary","io/output/d0",ConfigType::Bool,&digitalCfg_[0].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d0PulseVar_{NVS_KEY(NvsKeys::Io::IO_D0PM),"d0_pulse_ms","io/output/d0",ConfigType::Int32,&digitalCfg_[0].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d1NameVar_{NVS_KEY(NvsKeys::Io::IO_D1NM),"d1_name","io/output/d1",ConfigType::CharArray,(char*)digitalCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[1].name)};
    ConfigVariable<uint8_t,0> d1PinVar_{NVS_KEY(NvsKeys::Io::IO_D1PN),"d1_pin","io/output/d1",ConfigType::UInt8,&digitalCfg_[1].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D1AH),"d1_active_high","io/output/d1",ConfigType::Bool,&digitalCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D1IN),"d1_initial_on","io/output/d1",ConfigType::Bool,&digitalCfg_[1].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D1MO),"d1_momentary","io/output/d1",ConfigType::Bool,&digitalCfg_[1].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d1PulseVar_{NVS_KEY(NvsKeys::Io::IO_D1PM),"d1_pulse_ms","io/output/d1",ConfigType::Int32,&digitalCfg_[1].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d2NameVar_{NVS_KEY(NvsKeys::Io::IO_D2NM),"d2_name","io/output/d2",ConfigType::CharArray,(char*)digitalCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[2].name)};
    ConfigVariable<uint8_t,0> d2PinVar_{NVS_KEY(NvsKeys::Io::IO_D2PN),"d2_pin","io/output/d2",ConfigType::UInt8,&digitalCfg_[2].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D2AH),"d2_active_high","io/output/d2",ConfigType::Bool,&digitalCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D2IN),"d2_initial_on","io/output/d2",ConfigType::Bool,&digitalCfg_[2].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D2MO),"d2_momentary","io/output/d2",ConfigType::Bool,&digitalCfg_[2].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d2PulseVar_{NVS_KEY(NvsKeys::Io::IO_D2PM),"d2_pulse_ms","io/output/d2",ConfigType::Int32,&digitalCfg_[2].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d3NameVar_{NVS_KEY(NvsKeys::Io::IO_D3NM),"d3_name","io/output/d3",ConfigType::CharArray,(char*)digitalCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[3].name)};
    ConfigVariable<uint8_t,0> d3PinVar_{NVS_KEY(NvsKeys::Io::IO_D3PN),"d3_pin","io/output/d3",ConfigType::UInt8,&digitalCfg_[3].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D3AH),"d3_active_high","io/output/d3",ConfigType::Bool,&digitalCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D3IN),"d3_initial_on","io/output/d3",ConfigType::Bool,&digitalCfg_[3].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D3MO),"d3_momentary","io/output/d3",ConfigType::Bool,&digitalCfg_[3].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d3PulseVar_{NVS_KEY(NvsKeys::Io::IO_D3PM),"d3_pulse_ms","io/output/d3",ConfigType::Int32,&digitalCfg_[3].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d4NameVar_{NVS_KEY(NvsKeys::Io::IO_D4NM),"d4_name","io/output/d4",ConfigType::CharArray,(char*)digitalCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[4].name)};
    ConfigVariable<uint8_t,0> d4PinVar_{NVS_KEY(NvsKeys::Io::IO_D4PN),"d4_pin","io/output/d4",ConfigType::UInt8,&digitalCfg_[4].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D4AH),"d4_active_high","io/output/d4",ConfigType::Bool,&digitalCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D4IN),"d4_initial_on","io/output/d4",ConfigType::Bool,&digitalCfg_[4].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D4MO),"d4_momentary","io/output/d4",ConfigType::Bool,&digitalCfg_[4].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d4PulseVar_{NVS_KEY(NvsKeys::Io::IO_D4PM),"d4_pulse_ms","io/output/d4",ConfigType::Int32,&digitalCfg_[4].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d5NameVar_{NVS_KEY(NvsKeys::Io::IO_D5NM),"d5_name","io/output/d5",ConfigType::CharArray,(char*)digitalCfg_[5].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[5].name)};
    ConfigVariable<uint8_t,0> d5PinVar_{NVS_KEY(NvsKeys::Io::IO_D5PN),"d5_pin","io/output/d5",ConfigType::UInt8,&digitalCfg_[5].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D5AH),"d5_active_high","io/output/d5",ConfigType::Bool,&digitalCfg_[5].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D5IN),"d5_initial_on","io/output/d5",ConfigType::Bool,&digitalCfg_[5].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D5MO),"d5_momentary","io/output/d5",ConfigType::Bool,&digitalCfg_[5].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d5PulseVar_{NVS_KEY(NvsKeys::Io::IO_D5PM),"d5_pulse_ms","io/output/d5",ConfigType::Int32,&digitalCfg_[5].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d6NameVar_{NVS_KEY(NvsKeys::Io::IO_D6NM),"d6_name","io/output/d6",ConfigType::CharArray,(char*)digitalCfg_[6].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[6].name)};
    ConfigVariable<uint8_t,0> d6PinVar_{NVS_KEY(NvsKeys::Io::IO_D6PN),"d6_pin","io/output/d6",ConfigType::UInt8,&digitalCfg_[6].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D6AH),"d6_active_high","io/output/d6",ConfigType::Bool,&digitalCfg_[6].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D6IN),"d6_initial_on","io/output/d6",ConfigType::Bool,&digitalCfg_[6].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D6MO),"d6_momentary","io/output/d6",ConfigType::Bool,&digitalCfg_[6].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d6PulseVar_{NVS_KEY(NvsKeys::Io::IO_D6PM),"d6_pulse_ms","io/output/d6",ConfigType::Int32,&digitalCfg_[6].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d7NameVar_{NVS_KEY(NvsKeys::Io::IO_D7NM),"d7_name","io/output/d7",ConfigType::CharArray,(char*)digitalCfg_[7].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[7].name)};
    ConfigVariable<uint8_t,0> d7PinVar_{NVS_KEY(NvsKeys::Io::IO_D7PN),"d7_pin","io/output/d7",ConfigType::UInt8,&digitalCfg_[7].pin,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D7AH),"d7_active_high","io/output/d7",ConfigType::Bool,&digitalCfg_[7].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D7IN),"d7_initial_on","io/output/d7",ConfigType::Bool,&digitalCfg_[7].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D7MO),"d7_momentary","io/output/d7",ConfigType::Bool,&digitalCfg_[7].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d7PulseVar_{NVS_KEY(NvsKeys::Io::IO_D7PM),"d7_pulse_ms","io/output/d7",ConfigType::Int32,&digitalCfg_[7].pulseMs,ConfigPersistence::Persistent,0};
};
