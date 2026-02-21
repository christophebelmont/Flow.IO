#pragma once
/**
 * @file PoolLogicModule.h
 * @brief Pool business orchestration based on scheduler windows and sensor conditions.
 */

#include "Core/Module.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Layout/PoolIoMap.h"
#include "Core/Layout/PoolSensorMap.h"
#include "Core/Services/Services.h"
#include "Domain/PoolLogicDefaults.h"

/** @brief Event ids owned by PoolLogicModule. */
constexpr uint16_t POOLLOGIC_EVENT_DAILY_RECALC = 0x2101;
constexpr uint16_t POOLLOGIC_EVENT_FILTRATION_WINDOW = 0x2102;

class PoolLogicModule : public Module, public IRuntimeSnapshotProvider {
public:
    const char* moduleId() const override { return "poollogic"; }
    const char* taskName() const override { return "poollogic"; }
    BaseType_t taskCore() const override { return 1; }

    uint8_t dependencyCount() const override { return 8; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "eventbus";
        if (i == 2) return "time";
        if (i == 3) return "io";
        if (i == 4) return "pooldev";
        if (i == 5) return "ha";
        if (i == 6) return "cmd";
        if (i == 7) return "alarms";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;
    void setStartupReady(bool ready) { startupReady_ = ready; }

private:
    struct DeviceFsm {
        bool known = false;
        bool on = false;
        bool lastDesired = false;
        uint32_t stateSinceMs = 0;
        uint32_t lastCmdMs = 0;
    };

    struct TemporalPidState {
        bool initialized = false;
        bool sampleValid = false;
        bool lastDemandOn = false;
        uint32_t windowStartMs = 0;
        uint32_t lastComputeMs = 0;
        uint32_t sampleTsMs = 0;
        uint32_t outputOnMs = 0;
        uint32_t runtimeTsMs = 0;
        float sampleInput = 0.0f;
        float sampleSetpoint = 0.0f;
        float sampleError = 0.0f;
        float integral = 0.0f;
        float prevError = 0.0f;
        float lastError = 0.0f;
    };

    static constexpr uint8_t SLOT_DAILY_RECALC = 3;
    static constexpr uint8_t SLOT_FILTR_WINDOW = 4;

    static constexpr uint8_t IO_ID_PH_DEFAULT =
        (uint8_t)FLOW_POOL_SENSOR_BINDINGS[POOL_SENSOR_SLOT_PH].ioId;
    static constexpr uint8_t IO_ID_ORP_DEFAULT =
        (uint8_t)FLOW_POOL_SENSOR_BINDINGS[POOL_SENSOR_SLOT_ORP].ioId;
    static constexpr uint8_t IO_ID_PSI_DEFAULT =
        (uint8_t)FLOW_POOL_SENSOR_BINDINGS[POOL_SENSOR_SLOT_PSI].ioId;
    static constexpr uint8_t IO_ID_WATER_TEMP_DEFAULT =
        (uint8_t)FLOW_POOL_SENSOR_BINDINGS[POOL_SENSOR_SLOT_WATER_TEMP].ioId;
    static constexpr uint8_t IO_ID_AIR_TEMP_DEFAULT =
        (uint8_t)FLOW_POOL_SENSOR_BINDINGS[POOL_SENSOR_SLOT_AIR_TEMP].ioId;
    static constexpr uint8_t IO_ID_LEVEL_DEFAULT =
        (uint8_t)FLOW_POOL_SENSOR_BINDINGS[POOL_SENSOR_SLOT_POOL_LEVEL].ioId;

    bool enabled_ = true;
    volatile bool startupReady_ = true;

    // Modes
    bool autoMode_ = true;
    bool winterMode_ = false;
    bool phAutoMode_ = false;
    bool orpAutoMode_ = false;
    bool electrolyseMode_ = false;
    bool electroRunMode_ = false;

    // Schedule / filtration window from water temperature
    float waterTempLowThreshold_ = PoolDefaults::TempLow;
    float waterTempSetpoint_ = PoolDefaults::TempHigh;
    uint8_t filtrationStartMin_ = PoolDefaults::FiltrationStartMinHour;
    uint8_t filtrationStopMax_ = PoolDefaults::FiltrationStopMaxHour;
    uint8_t filtrationCalcStart_ = PoolDefaults::FiltrationStartMinHour;
    uint8_t filtrationCalcStop_ = PoolDefaults::FiltrationStopMaxHour;

    // Sensor IO ids for IOServiceV2 reads.
    // Stored as uint8_t because current static id map stays <= 255.
    uint8_t phIoId_ = IO_ID_PH_DEFAULT;
    uint8_t orpIoId_ = IO_ID_ORP_DEFAULT;
    uint8_t psiIoId_ = IO_ID_PSI_DEFAULT;
    uint8_t waterTempIoId_ = IO_ID_WATER_TEMP_DEFAULT;
    uint8_t airTempIoId_ = IO_ID_AIR_TEMP_DEFAULT;
    uint8_t levelIoId_ = IO_ID_LEVEL_DEFAULT;

    // Thresholds / delays
    float psiLowThreshold_ = 0.15f;
    float psiHighThreshold_ = 1.80f;
    float winterStartTempC_ = -2.0f;
    float freezeHoldTempC_ = 2.0f;
    float secureElectroTempC_ = 15.0f;
    float phSetpoint_ = PoolDefaults::PhSetpoint;
    float orpSetpoint_ = PoolDefaults::OrpSetpoint;
    float phKp_ = PoolDefaults::PhKp;
    float phKi_ = PoolDefaults::PhKi;
    float phKd_ = PoolDefaults::PhKd;
    float orpKp_ = PoolDefaults::OrpKp;
    float orpKi_ = PoolDefaults::OrpKi;
    float orpKd_ = PoolDefaults::OrpKd;
    int32_t phWindowMs_ = PoolDefaults::PidWindowMs;
    int32_t orpWindowMs_ = PoolDefaults::PidWindowMs;
    int32_t pidMinOnMs_ = PoolDefaults::PidMinOnMs;
    int32_t pidSampleMs_ = PoolDefaults::PidSampleMs;
    uint8_t psiStartupDelaySec_ = 60;
    uint8_t delayPidsMin_ = 5;
    uint8_t delayElectroMin_ = 10;
    uint8_t robotDelayMin_ = 30;
    uint8_t robotDurationMin_ = 120;
    uint8_t fillingMinOnSec_ = 30;

    // Controlled pool devices
    uint8_t filtrationDeviceSlot_ = POOL_IO_SLOT_FILTRATION_PUMP;
    uint8_t swgDeviceSlot_ = POOL_IO_SLOT_CHLORINE_GENERATOR;
    uint8_t robotDeviceSlot_ = POOL_IO_SLOT_ROBOT;
    uint8_t fillingDeviceSlot_ = POOL_IO_SLOT_FILL_PUMP;
    uint8_t phPumpDeviceSlot_ = POOL_IO_SLOT_PH_PUMP;
    uint8_t orpPumpDeviceSlot_ = POOL_IO_SLOT_CHLORINE_PUMP;

    // Runtime flags
    DeviceFsm filtrationFsm_{};
    DeviceFsm swgFsm_{};
    DeviceFsm robotFsm_{};
    DeviceFsm fillingFsm_{};
    DeviceFsm phPumpFsm_{};
    DeviceFsm orpPumpFsm_{};
    TemporalPidState phPidState_{};
    TemporalPidState orpPidState_{};

    bool filtrationWindowActive_ = false;
    bool pendingDailyRecalc_ = false;
    bool pendingDayReset_ = false;

    bool psiError_ = false;
    bool cleaningDone_ = false;
    bool phPidEnabled_ = false;
    bool orpPidEnabled_ = false;

    portMUX_TYPE pendingMux_ = portMUX_INITIALIZER_UNLOCKED;

    ConfigStore* cfgStore_ = nullptr;
    EventBus* eventBus_ = nullptr;
    const TimeSchedulerService* schedSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const PoolDeviceService* poolSvc_ = nullptr;
    const HAService* haSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const LogHubService* logHub_ = nullptr;

    ConfigVariable<bool,0> enabledVar_{NVS_KEY(NvsKeys::PoolLogic::Enabled), "enabled", "poollogic", ConfigType::Bool,
                                       &enabled_, ConfigPersistence::Persistent, 0};

    ConfigVariable<bool,0> autoModeVar_{NVS_KEY(NvsKeys::PoolLogic::AutoMode), "auto_mode", "poollogic", ConfigType::Bool,
                                        &autoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> winterModeVar_{NVS_KEY(NvsKeys::PoolLogic::WinterMode), "winter_mode", "poollogic", ConfigType::Bool,
                                          &winterMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> phAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::PhAutoMode), "ph_auto_mode", "poollogic", ConfigType::Bool,
                                          &phAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> orpAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::OrpAutoMode), "orp_auto_mode", "poollogic", ConfigType::Bool,
                                           &orpAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> electrolyseModeVar_{NVS_KEY(NvsKeys::PoolLogic::ElectrolyseMode), "elec_mode", "poollogic", ConfigType::Bool,
                                               &electrolyseMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> electroRunModeVar_{NVS_KEY(NvsKeys::PoolLogic::ElectroRunMode), "elec_run", "poollogic", ConfigType::Bool,
                                              &electroRunMode_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> tempLowVar_{NVS_KEY(NvsKeys::PoolLogic::TempLow), "wat_temp_lo_th", "poollogic", ConfigType::Float,
                                        &waterTempLowThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> tempSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::TempSetpoint), "wat_temp_setpt", "poollogic", ConfigType::Float,
                                             &waterTempSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> startMinVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationStartMin), "filtr_start_min", "poollogic", ConfigType::UInt8,
                                           &filtrationStartMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> stopMaxVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationStopMax), "filtr_stop_max", "poollogic", ConfigType::UInt8,
                                          &filtrationStopMax_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> calcStartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStart), "filtr_start_clc", "poollogic", ConfigType::UInt8,
                                            &filtrationCalcStart_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> calcStopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStop), "filtr_stop_clc", "poollogic", ConfigType::UInt8,
                                           &filtrationCalcStop_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> phIdVar_{NVS_KEY(NvsKeys::PoolLogic::PhIoId), "ph_io_id", "poollogic", ConfigType::UInt8,
                                       &phIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> orpIdVar_{NVS_KEY(NvsKeys::PoolLogic::OrpIoId), "orp_io_id", "poollogic", ConfigType::UInt8,
                                        &orpIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> psiIdVar_{NVS_KEY(NvsKeys::PoolLogic::PsiIoId), "psi_io_id", "poollogic", ConfigType::UInt8,
                                        &psiIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> waterTempIdVar_{NVS_KEY(NvsKeys::PoolLogic::WaterTempIoId), "wat_temp_io_id", "poollogic", ConfigType::UInt8,
                                              &waterTempIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> airTempIdVar_{NVS_KEY(NvsKeys::PoolLogic::AirTempIoId), "air_temp_io_id", "poollogic", ConfigType::UInt8,
                                            &airTempIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> levelIdVar_{NVS_KEY(NvsKeys::PoolLogic::LevelIoId), "pool_lvl_io_id", "poollogic", ConfigType::UInt8,
                                          &levelIoId_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> psiLowVar_{NVS_KEY(NvsKeys::PoolLogic::PsiLow), "psi_low_th", "poollogic", ConfigType::Float,
                                       &psiLowThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> psiHighVar_{NVS_KEY(NvsKeys::PoolLogic::PsiHigh), "psi_high_th", "poollogic", ConfigType::Float,
                                        &psiHighThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> winterStartVar_{NVS_KEY(NvsKeys::PoolLogic::WinterStart), "winter_start_t", "poollogic", ConfigType::Float,
                                            &winterStartTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> freezeHoldVar_{NVS_KEY(NvsKeys::PoolLogic::FreezeHold), "freeze_hold_t", "poollogic", ConfigType::Float,
                                           &freezeHoldTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> secureElectroVar_{NVS_KEY(NvsKeys::PoolLogic::SecureElectro), "secure_elec_t", "poollogic", ConfigType::Float,
                                              &secureElectroTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::PhSetpoint), "ph_setpoint", "poollogic", ConfigType::Float,
                                           &phSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::OrpSetpoint), "orp_setpoint", "poollogic", ConfigType::Float,
                                            &orpSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phKpVar_{NVS_KEY(NvsKeys::PoolLogic::PhKp), "ph_kp", "poollogic", ConfigType::Float,
                                     &phKp_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phKiVar_{NVS_KEY(NvsKeys::PoolLogic::PhKi), "ph_ki", "poollogic", ConfigType::Float,
                                     &phKi_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phKdVar_{NVS_KEY(NvsKeys::PoolLogic::PhKd), "ph_kd", "poollogic", ConfigType::Float,
                                     &phKd_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKpVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKp), "orp_kp", "poollogic", ConfigType::Float,
                                      &orpKp_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKiVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKi), "orp_ki", "poollogic", ConfigType::Float,
                                      &orpKi_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKdVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKd), "orp_kd", "poollogic", ConfigType::Float,
                                      &orpKd_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> phWindowMsVar_{NVS_KEY(NvsKeys::PoolLogic::PhWindowMs), "ph_window_ms", "poollogic", ConfigType::Int32,
                                             &phWindowMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> orpWindowMsVar_{NVS_KEY(NvsKeys::PoolLogic::OrpWindowMs), "orp_window_ms", "poollogic", ConfigType::Int32,
                                              &orpWindowMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> pidMinOnMsVar_{NVS_KEY(NvsKeys::PoolLogic::PidMinOnMs), "pid_min_on_ms", "poollogic", ConfigType::Int32,
                                             &pidMinOnMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> pidSampleMsVar_{NVS_KEY(NvsKeys::PoolLogic::PidSampleMs), "pid_sample_ms", "poollogic", ConfigType::Int32,
                                              &pidSampleMs_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> psiDelayVar_{NVS_KEY(NvsKeys::PoolLogic::PsiDelay), "psi_start_dly_s", "poollogic", ConfigType::UInt8,
                                           &psiStartupDelaySec_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> delayPidsVar_{NVS_KEY(NvsKeys::PoolLogic::DelayPids), "dly_pid_min", "poollogic", ConfigType::UInt8,
                                            &delayPidsMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> delayElectroVar_{NVS_KEY(NvsKeys::PoolLogic::DelayElectro), "dly_electro_min", "poollogic", ConfigType::UInt8,
                                               &delayElectroMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDelayVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDelay), "robot_delay_min", "poollogic", ConfigType::UInt8,
                                             &robotDelayMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDurationVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDuration), "robot_dur_min", "poollogic", ConfigType::UInt8,
                                                &robotDurationMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> fillingMinOnVar_{NVS_KEY(NvsKeys::PoolLogic::FillingMinOn), "fill_min_on_s", "poollogic", ConfigType::UInt8,
                                               &fillingMinOnSec_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> filtrationDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationSlot), "filtr_slot", "poollogic", ConfigType::UInt8,
                                                   &filtrationDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> swgDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::SwgSlot), "swg_slot", "poollogic", ConfigType::UInt8,
                                            &swgDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::RobotSlot), "robot_slot", "poollogic", ConfigType::UInt8,
                                              &robotDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> fillingDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::FillingSlot), "fill_slot", "poollogic", ConfigType::UInt8,
                                                &fillingDeviceSlot_, ConfigPersistence::Persistent, 0};

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    static bool cmdFiltrationWriteStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFiltrationRecalcStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdAutoModeSetStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static AlarmCondState condPsiLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPsiHighStatic_(void* ctx, uint32_t nowMs);
    bool cmdFiltrationWrite_(const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdFiltrationRecalc_(const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdAutoModeSet_(const CommandRequest& req, char* reply, size_t replyLen);

    void ensureDailySlot_();
    bool computeFiltrationWindow_(float waterTemp, uint8_t& startHourOut, uint8_t& stopHourOut, uint8_t& durationOut);
    bool recalcAndApplyFiltrationWindow_(uint8_t* startHourOut = nullptr,
                                         uint8_t* stopHourOut = nullptr,
                                         uint8_t* durationOut = nullptr);

    bool readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const;
    bool writeDeviceDesired_(uint8_t deviceSlot, bool on);

    void syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut);
    uint32_t stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const;
    bool loadAnalogSensor_(uint8_t ioId, float& out) const;
    bool loadDigitalSensor_(uint8_t ioId, bool& out) const;
    void resetTemporalPidState_(TemporalPidState& st, uint32_t nowMs);
    bool stepTemporalPid_(TemporalPidState& st,
                          float input,
                          float setpoint,
                          float kp,
                          float ki,
                          float kd,
                          int32_t windowMsCfg,
                          bool positiveWhenInputHigh,
                          uint32_t nowMs,
                          bool& demandOnOut,
                          uint32_t& outputOnMsOut);

    void applyDeviceControl_(uint8_t deviceSlot, const char* label, DeviceFsm& fsm, bool desired, uint32_t nowMs);
    void runControlLoop_(uint32_t nowMs);
};
