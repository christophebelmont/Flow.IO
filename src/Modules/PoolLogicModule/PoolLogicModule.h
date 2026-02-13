#pragma once
/**
 * @file PoolLogicModule.h
 * @brief Pool business orchestration based on scheduler windows and sensor conditions.
 */

#include "Core/Module.h"
#include "Core/ConfigTypes.h"
#include "Core/Layout/PoolSensorMap.h"
#include "Core/Services/Services.h"

/** @brief Event ids owned by PoolLogicModule. */
constexpr uint16_t POOLLOGIC_EVENT_DAILY_RECALC = 0x2101;
constexpr uint16_t POOLLOGIC_EVENT_FILTRATION_WINDOW = 0x2102;

class PoolLogicModule : public Module {
public:
    const char* moduleId() const override { return "poollogic"; }
    const char* taskName() const override { return "poollogic"; }

    uint8_t dependencyCount() const override { return 5; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "eventbus";
        if (i == 2) return "time";
        if (i == 3) return "io";
        if (i == 4) return "pooldev";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    struct DeviceFsm {
        bool known = false;
        bool on = false;
        bool lastDesired = false;
        uint32_t stateSinceMs = 0;
        uint32_t lastCmdMs = 0;
    };

    static constexpr uint8_t SLOT_DAILY_RECALC = 3;
    static constexpr uint8_t SLOT_FILTR_WINDOW = 4;

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

    // Modes
    bool autoMode_ = true;
    bool winterMode_ = false;
    bool phAutoMode_ = false;
    bool orpAutoMode_ = false;
    bool electrolyseMode_ = false;
    bool electroRunMode_ = false;

    // Schedule / filtration window from water temperature
    float waterTempLowThreshold_ = 12.0f;
    float waterTempSetpoint_ = 24.0f;
    uint8_t filtrationStartMin_ = 8;
    uint8_t filtrationStopMax_ = 23;

    // Sensor IO ids for IOServiceV2 reads.
    // Stored as uint8_t because current static id map stays <= 255.
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
    float orpSetpoint_ = 700.0f;
    uint8_t psiStartupDelaySec_ = 180;
    uint8_t delayPidsMin_ = 5;
    uint8_t delayElectroMin_ = 10;
    uint8_t robotDelayMin_ = 30;
    uint8_t robotDurationMin_ = 120;
    uint8_t fillingMinOnSec_ = 30;

    // Controlled pool devices
    uint8_t filtrationDeviceSlot_ = 0;
    uint8_t swgDeviceSlot_ = 2;
    uint8_t robotDeviceSlot_ = 3;
    uint8_t fillingDeviceSlot_ = 4;

    // Runtime flags
    DeviceFsm filtrationFsm_{};
    DeviceFsm swgFsm_{};
    DeviceFsm robotFsm_{};
    DeviceFsm fillingFsm_{};

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
    const LogHubService* logHub_ = nullptr;

    ConfigVariable<bool,0> enabledVar_{NVS_KEY("pl_en"), "enabled", "poollogic", ConfigType::Bool,
                                       &enabled_, ConfigPersistence::Persistent, 0};

    ConfigVariable<bool,0> autoModeVar_{NVS_KEY("pl_auto"), "auto_mode", "poollogic", ConfigType::Bool,
                                        &autoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> winterModeVar_{NVS_KEY("pl_wint"), "winter_mode", "poollogic", ConfigType::Bool,
                                          &winterMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> phAutoModeVar_{NVS_KEY("pl_pha"), "ph_auto_mode", "poollogic", ConfigType::Bool,
                                          &phAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> orpAutoModeVar_{NVS_KEY("pl_orpa"), "orp_auto_mode", "poollogic", ConfigType::Bool,
                                           &orpAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> electrolyseModeVar_{NVS_KEY("pl_elec"), "electrolyse_mode", "poollogic", ConfigType::Bool,
                                               &electrolyseMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> electroRunModeVar_{NVS_KEY("pl_erun"), "electro_run_mode", "poollogic", ConfigType::Bool,
                                              &electroRunMode_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> tempLowVar_{NVS_KEY("pl_tlow"), "water_temp_low_threshold", "poollogic", ConfigType::Float,
                                        &waterTempLowThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> tempSetpointVar_{NVS_KEY("pl_tset"), "water_temp_setpoint", "poollogic", ConfigType::Float,
                                             &waterTempSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> startMinVar_{NVS_KEY("pl_smin"), "filtration_start_min", "poollogic", ConfigType::UInt8,
                                           &filtrationStartMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> stopMaxVar_{NVS_KEY("pl_smax"), "filtration_stop_max", "poollogic", ConfigType::UInt8,
                                          &filtrationStopMax_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> orpIdVar_{NVS_KEY("pl_oiid"), "orp_io_id", "poollogic", ConfigType::UInt8,
                                        &orpIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> psiIdVar_{NVS_KEY("pl_piid"), "psi_io_id", "poollogic", ConfigType::UInt8,
                                        &psiIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> waterTempIdVar_{NVS_KEY("pl_wiid"), "water_temp_io_id", "poollogic", ConfigType::UInt8,
                                              &waterTempIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> airTempIdVar_{NVS_KEY("pl_aiid"), "air_temp_io_id", "poollogic", ConfigType::UInt8,
                                            &airTempIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> levelIdVar_{NVS_KEY("pl_liid"), "pool_level_io_id", "poollogic", ConfigType::UInt8,
                                          &levelIoId_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> psiLowVar_{NVS_KEY("pl_psil"), "psi_low_threshold", "poollogic", ConfigType::Float,
                                       &psiLowThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> psiHighVar_{NVS_KEY("pl_psih"), "psi_high_threshold", "poollogic", ConfigType::Float,
                                        &psiHighThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> winterStartVar_{NVS_KEY("pl_wstr"), "winter_start_temp", "poollogic", ConfigType::Float,
                                            &winterStartTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> freezeHoldVar_{NVS_KEY("pl_whld"), "freeze_hold_temp", "poollogic", ConfigType::Float,
                                           &freezeHoldTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> secureElectroVar_{NVS_KEY("pl_sect"), "secure_electro_temp", "poollogic", ConfigType::Float,
                                              &secureElectroTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpSetpointVar_{NVS_KEY("pl_orps"), "orp_setpoint", "poollogic", ConfigType::Float,
                                            &orpSetpoint_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> psiDelayVar_{NVS_KEY("pl_psdt"), "psi_startup_delay_sec", "poollogic", ConfigType::UInt8,
                                           &psiStartupDelaySec_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> delayPidsVar_{NVS_KEY("pl_dpds"), "delay_pids_min", "poollogic", ConfigType::UInt8,
                                            &delayPidsMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> delayElectroVar_{NVS_KEY("pl_delt"), "delay_electro_min", "poollogic", ConfigType::UInt8,
                                               &delayElectroMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDelayVar_{NVS_KEY("pl_rdel"), "robot_delay_min", "poollogic", ConfigType::UInt8,
                                             &robotDelayMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDurationVar_{NVS_KEY("pl_rdur"), "robot_duration_min", "poollogic", ConfigType::UInt8,
                                                &robotDurationMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> fillingMinOnVar_{NVS_KEY("pl_fmin"), "filling_min_on_sec", "poollogic", ConfigType::UInt8,
                                               &fillingMinOnSec_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> filtrationDeviceVar_{NVS_KEY("pl_sfil"), "filtration_slot", "poollogic", ConfigType::UInt8,
                                                   &filtrationDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> swgDeviceVar_{NVS_KEY("pl_sswg"), "swg_slot", "poollogic", ConfigType::UInt8,
                                            &swgDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDeviceVar_{NVS_KEY("pl_srob"), "robot_slot", "poollogic", ConfigType::UInt8,
                                              &robotDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> fillingDeviceVar_{NVS_KEY("pl_sfill"), "filling_slot", "poollogic", ConfigType::UInt8,
                                                &fillingDeviceSlot_, ConfigPersistence::Persistent, 0};

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);

    void ensureDailySlot_();
    bool computeFiltrationWindow_(float waterTemp, uint8_t& startHourOut, uint8_t& stopHourOut, uint8_t& durationOut);
    void recalcAndApplyFiltrationWindow_();

    bool readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const;
    bool writeDeviceDesired_(uint8_t deviceSlot, bool on);

    void syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut);
    uint32_t stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const;
    bool loadAnalogSensor_(uint8_t ioId, float& out) const;
    bool loadDigitalSensor_(uint8_t ioId, bool& out) const;

    void applyDeviceControl_(uint8_t deviceSlot, const char* label, DeviceFsm& fsm, bool desired, uint32_t nowMs);
    void runControlLoop_(uint32_t nowMs);
};
