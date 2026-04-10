#pragma once
/**
 * @file PoolLogicModule.h
 * @brief Pool business orchestration based on scheduler windows and sensor conditions.
 *
 * Public facade only. The implementation is split across Lifecycle / Scheduler /
 * Control / Runtime / Commands translation units.
 */

#include "Core/Module.h"
#include "Core/RuntimeUi.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolBindings.h"
#include "Domain/Pool/PoolDefaults.h"

/** @brief Event ids owned by PoolLogicModule. */
constexpr uint16_t POOLLOGIC_EVENT_DAILY_RECALC = 0x2101;
constexpr uint16_t POOLLOGIC_EVENT_FILTRATION_WINDOW = 0x2102;

class PoolLogicModule : public Module, public IRuntimeSnapshotProvider, public IRuntimeUiValueProvider {
public:
    ModuleId moduleId() const override { return ModuleId::PoolLogic; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }
    const char* taskName() const override { return "poollogic"; }
    BaseType_t taskCore() const override { return 1; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 8; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::EventBus;
        if (i == 2) return ModuleId::Time;
        if (i == 3) return ModuleId::Io;
        if (i == 4) return ModuleId::PoolDevice;
        if (i == 5) return ModuleId::Ha;
        if (i == 6) return ModuleId::Command;
        if (i == 7) return ModuleId::Alarm;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint32_t startDelayMs() const override { return Limits::Boot::PoolLogicStartDelayMs; }
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    RuntimeRouteClass runtimeSnapshotClass(uint8_t idx) const override;
    bool runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;
    static MqttBuildResult buildCfgBaseStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);

private:
    enum RuntimeUiValueId : uint8_t {
        RuntimeUiAutoMode = 1,
        RuntimeUiWinterMode = 2,
        RuntimeUiPhAutoMode = 3,
        RuntimeUiOrpAutoMode = 4,
    };

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

    static constexpr IoId IO_ID_PH_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPh].ioId;
    static constexpr IoId IO_ID_ORP_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotOrp].ioId;
    static constexpr IoId IO_ID_PSI_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPsi].ioId;
    static constexpr IoId IO_ID_WATER_TEMP_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotWaterTemp].ioId;
    static constexpr IoId IO_ID_AIR_TEMP_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotAirTemp].ioId;
    static constexpr IoId IO_ID_LEVEL_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPoolLevel].ioId;
    static constexpr IoId IO_ID_PH_LEVEL_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPhLevel].ioId;
    static constexpr IoId IO_ID_CHLORINE_LEVEL_DEFAULT =
        PoolBinding::kSensorBindings[PoolBinding::kSensorSlotChlorineLevel].ioId;

    // State and configuration storage
    bool enabled_ = true;

    // Modes
    bool autoMode_ = true;
    bool winterMode_ = false;
    bool phAutoMode_ = false;
    bool orpAutoMode_ = false;
    bool phDosePlus_ = false;
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
    IoId phIoId_ = IO_ID_PH_DEFAULT;
    IoId orpIoId_ = IO_ID_ORP_DEFAULT;
    IoId psiIoId_ = IO_ID_PSI_DEFAULT;
    IoId waterTempIoId_ = IO_ID_WATER_TEMP_DEFAULT;
    IoId airTempIoId_ = IO_ID_AIR_TEMP_DEFAULT;
    IoId levelIoId_ = IO_ID_LEVEL_DEFAULT;
    IoId phLevelIoId_ = IO_ID_PH_LEVEL_DEFAULT;
    IoId chlorineLevelIoId_ = IO_ID_CHLORINE_LEVEL_DEFAULT;

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
    uint8_t filtrationDeviceSlot_ = PoolBinding::kDeviceSlotFiltrationPump;
    uint8_t swgDeviceSlot_ = PoolBinding::kDeviceSlotChlorineGenerator;
    uint8_t robotDeviceSlot_ = PoolBinding::kDeviceSlotRobot;
    uint8_t fillingDeviceSlot_ = PoolBinding::kDeviceSlotFillPump;
    uint8_t phPumpDeviceSlot_ = PoolBinding::kDeviceSlotPhPump;
    uint8_t orpPumpDeviceSlot_ = PoolBinding::kDeviceSlotChlorinePump;

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
    bool pendingFiltrationReconcile_ = false;

    bool psiError_ = false;
    bool phTankLowError_ = false;
    bool chlorineTankLowError_ = false;
    bool cleaningDone_ = false;
    bool phPidEnabled_ = false;
    bool orpPidEnabled_ = false;

    portMUX_TYPE pendingMux_ = portMUX_INITIALIZER_UNLOCKED;

    // CFGDOC: {"label":"PoolLogic actif","help":"Active ou désactive les automatismes PoolLogic."}
    ConfigVariable<bool,0> enabledVar_{NVS_KEY(NvsKeys::PoolLogic::Enabled), "enabled", "poollogic", ConfigType::Bool,
                                       &enabled_, ConfigPersistence::Persistent, 0};

    // CFGDOC: {"label":"Mode auto global","help":"Active le pilotage automatique global de la piscine."}
    ConfigVariable<bool,0> autoModeVar_{NVS_KEY(NvsKeys::PoolLogic::AutoMode), "auto_mode", "poollogic", ConfigType::Bool,
                                        &autoMode_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Mode hiver force","help":"Force le fonctionnement en mode hiver (anti-gel)."}
    ConfigVariable<bool,0> winterModeVar_{NVS_KEY(NvsKeys::PoolLogic::WinterMode), "winter_mode", "poollogic", ConfigType::Bool,
                                          &winterMode_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Mode auto pH","help":"Active la régulation automatique du pH."}
    ConfigVariable<bool,0> phAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::PhAutoMode), "ph_auto_mode", "poollogic", ConfigType::Bool,
                                          &phAutoMode_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Mode auto ORP","help":"Active la régulation automatique ORP/chlore."}
    ConfigVariable<bool,0> orpAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::OrpAutoMode), "orp_auto_mode", "poollogic", ConfigType::Bool,
                                           &orpAutoMode_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Injection pH+","help":"Si activé, la pompe pH injecte du pH+ (sinon pH-)."}
    ConfigVariable<bool,0> phDosePlusVar_{NVS_KEY(NvsKeys::PoolLogic::PhDosePlus), "ph_dose_plus", "poollogic", ConfigType::Bool,
                                          &phDosePlus_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Electrolyse active","help":"Autorise l'usage de l'electrolyseur."}
    ConfigVariable<bool,0> electrolyseModeVar_{NVS_KEY(NvsKeys::PoolLogic::ElectrolyseMode), "elec_mode", "poollogic", ConfigType::Bool,
                                               &electrolyseMode_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Electrolyse en service","help":"Autorise la commande de marche de l'electrolyseur."}
    ConfigVariable<bool,0> electroRunModeVar_{NVS_KEY(NvsKeys::PoolLogic::ElectroRunMode), "elec_run", "poollogic", ConfigType::Bool,
                                              &electroRunMode_, ConfigPersistence::Persistent, 0};

    // CFGDOC: {"label":"Seuil eau basse (C)","help":"Seuil bas de température d'eau pour la logique de filtration.","unit":"C"}
    ConfigVariable<float,0> tempLowVar_{NVS_KEY(NvsKeys::PoolLogic::TempLow), "wat_temp_lo_th", "poollogic", ConfigType::Float,
                                        &waterTempLowThreshold_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Consigne eau (C)","help":"Consigne de température d'eau pour le calcul de filtration.","unit":"C"}
    ConfigVariable<float,0> tempSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::TempSetpoint), "wat_temp_setpt", "poollogic", ConfigType::Float,
                                             &waterTempSetpoint_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Début filtration min","help":"Heure minimale autorisée pour démarrer la filtration."}
    ConfigVariable<uint8_t,0> startMinVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationStartMin), "filtr_start_min", "poollogic", ConfigType::UInt8,
                                           &filtrationStartMin_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Arrêt filtration max","help":"Heure maximale autorisée pour arrêter la filtration."}
    ConfigVariable<uint8_t,0> stopMaxVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationStopMax), "filtr_stop_max", "poollogic", ConfigType::UInt8,
                                          &filtrationStopMax_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Début filtration calculé","help":"Heure de début de filtration calculée automatiquement."}
    ConfigVariable<uint8_t,0> calcStartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStart), "filtr_start_clc", "poollogic", ConfigType::UInt8,
                                            &filtrationCalcStart_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Arrêt filtration calculé","help":"Heure de fin de filtration calculée automatiquement."}
    ConfigVariable<uint8_t,0> calcStopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStop), "filtr_stop_clc", "poollogic", ConfigType::UInt8,
                                           &filtrationCalcStop_, ConfigPersistence::Persistent, 0};

    // CFGDOC: {"label":"IO capteur pH","help":"Identifiant IO de la mesure pH."}
    ConfigVariable<IoId,0> phIdVar_{NVS_KEY(NvsKeys::PoolLogic::PhIoId), "ph_io_id", "poollogic", ConfigType::UInt16,
                                       &phIoId_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"IO capteur ORP","help":"Identifiant IO de la mesure ORP."}
    ConfigVariable<IoId,0> orpIdVar_{NVS_KEY(NvsKeys::PoolLogic::OrpIoId), "orp_io_id", "poollogic", ConfigType::UInt16,
                                        &orpIoId_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"IO pression","help":"Identifiant IO du capteur de pression."}
    ConfigVariable<IoId,0> psiIdVar_{NVS_KEY(NvsKeys::PoolLogic::PsiIoId), "psi_io_id", "poollogic", ConfigType::UInt16,
                                        &psiIoId_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"IO température eau","help":"Identifiant IO de la sonde température eau."}
    ConfigVariable<IoId,0> waterTempIdVar_{NVS_KEY(NvsKeys::PoolLogic::WaterTempIoId), "wat_temp_io_id", "poollogic", ConfigType::UInt16,
                                              &waterTempIoId_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"IO température air","help":"Identifiant IO de la sonde température air."}
    ConfigVariable<IoId,0> airTempIdVar_{NVS_KEY(NvsKeys::PoolLogic::AirTempIoId), "air_temp_io_id", "poollogic", ConfigType::UInt16,
                                            &airTempIoId_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"IO niveau bassin","help":"Identifiant IO de la mesure de niveau bassin."}
    ConfigVariable<IoId,0> levelIdVar_{NVS_KEY(NvsKeys::PoolLogic::LevelIoId), "pool_lvl_io_id", "poollogic", ConfigType::UInt16,
                                          &levelIoId_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"IO niveau cuve pH","help":"Identifiant IO de l'entrée digitale niveau bas cuve pH."}
    ConfigVariable<IoId,0> phLevelIdVar_{NVS_KEY(NvsKeys::PoolLogic::PhLevelIoId), "ph_lvl_io_id", "poollogic", ConfigType::UInt16,
                                            &phLevelIoId_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"IO niveau cuve chlore","help":"Identifiant IO de l'entrée digitale niveau bas cuve chlore."}
    ConfigVariable<IoId,0> chlorineLevelIdVar_{NVS_KEY(NvsKeys::PoolLogic::ChlorineLevelIoId), "chl_lvl_io_id", "poollogic", ConfigType::UInt16,
                                                  &chlorineLevelIoId_, ConfigPersistence::Persistent, 0};

    // CFGDOC: {"label":"Seuil pression basse","help":"Seuil de pression basse pour detection d'anomalie.","unit":"bar"}
    ConfigVariable<float,0> psiLowVar_{NVS_KEY(NvsKeys::PoolLogic::PsiLow), "psi_low_th", "poollogic", ConfigType::Float,
                                       &psiLowThreshold_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Seuil pression haute","help":"Seuil de pression haute pour detection d'anomalie.","unit":"bar"}
    ConfigVariable<float,0> psiHighVar_{NVS_KEY(NvsKeys::PoolLogic::PsiHigh), "psi_high_th", "poollogic", ConfigType::Float,
                                        &psiHighThreshold_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Seuil entrée hiver (C)","help":"Temperature de bascule vers la logique hiver.","unit":"C"}
    ConfigVariable<float,0> winterStartVar_{NVS_KEY(NvsKeys::PoolLogic::WinterStart), "winter_start_t", "poollogic", ConfigType::Float,
                                            &winterStartTempC_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Seuil maintien hors gel (C)","help":"Temperature de maintien pour la protection hors gel.","unit":"C"}
    ConfigVariable<float,0> freezeHoldVar_{NVS_KEY(NvsKeys::PoolLogic::FreezeHold), "freeze_hold_t", "poollogic", ConfigType::Float,
                                           &freezeHoldTempC_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Seuil sécurité electrolyse (C)","help":"Temperature minimale autorisée pour l'electrolyse.","unit":"C"}
    ConfigVariable<float,0> secureElectroVar_{NVS_KEY(NvsKeys::PoolLogic::SecureElectro), "secure_elec_t", "poollogic", ConfigType::Float,
                                              &secureElectroTempC_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Consigne pH","help":"Valeur cible de pH pour la régulation."}
    ConfigVariable<float,0> phSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::PhSetpoint), "ph_setpoint", "poollogic", ConfigType::Float,
                                           &phSetpoint_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Consigne ORP","help":"Valeur cible ORP pour la régulation chlore."}
    ConfigVariable<float,0> orpSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::OrpSetpoint), "orp_setpoint", "poollogic", ConfigType::Float,
                                            &orpSetpoint_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"pH Kp","help":"Gain proportionnel du régulateur pH."}
    ConfigVariable<float,0> phKpVar_{NVS_KEY(NvsKeys::PoolLogic::PhKp), "ph_kp", "poollogic", ConfigType::Float,
                                     &phKp_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"pH Ki","help":"Gain intégral du régulateur pH."}
    ConfigVariable<float,0> phKiVar_{NVS_KEY(NvsKeys::PoolLogic::PhKi), "ph_ki", "poollogic", ConfigType::Float,
                                     &phKi_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"pH Kd","help":"Gain dérivé du régulateur pH."}
    ConfigVariable<float,0> phKdVar_{NVS_KEY(NvsKeys::PoolLogic::PhKd), "ph_kd", "poollogic", ConfigType::Float,
                                     &phKd_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"ORP Kp","help":"Gain proportionnel du régulateur ORP."}
    ConfigVariable<float,0> orpKpVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKp), "orp_kp", "poollogic", ConfigType::Float,
                                      &orpKp_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"ORP Ki","help":"Gain intégral du régulateur ORP."}
    ConfigVariable<float,0> orpKiVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKi), "orp_ki", "poollogic", ConfigType::Float,
                                      &orpKi_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"ORP Kd","help":"Gain dérivé du régulateur ORP."}
    ConfigVariable<float,0> orpKdVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKd), "orp_kd", "poollogic", ConfigType::Float,
                                      &orpKd_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Fenêtre pH (ms)","help":"Fenêtre temporelle PWM appliquée a la pompe pH.","unit":"ms"}
    ConfigVariable<int32_t,0> phWindowMsVar_{NVS_KEY(NvsKeys::PoolLogic::PhWindowMs), "ph_window_ms", "poollogic", ConfigType::Int32,
                                             &phWindowMs_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Fenêtre ORP (ms)","help":"Fenêtre temporelle PWM appliquée a la pompe ORP.","unit":"ms"}
    ConfigVariable<int32_t,0> orpWindowMsVar_{NVS_KEY(NvsKeys::PoolLogic::OrpWindowMs), "orp_window_ms", "poollogic", ConfigType::Int32,
                                              &orpWindowMs_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Temps min ON PID (ms)","help":"Durée minimale ON appliquée aux sorties PID.","unit":"ms"}
    ConfigVariable<int32_t,0> pidMinOnMsVar_{NVS_KEY(NvsKeys::PoolLogic::PidMinOnMs), "pid_min_on_ms", "poollogic", ConfigType::Int32,
                                             &pidMinOnMs_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Période échantillonnage PID (ms)","help":"Intervalle de calcul des régulateurs PID.","unit":"ms"}
    ConfigVariable<int32_t,0> pidSampleMsVar_{NVS_KEY(NvsKeys::PoolLogic::PidSampleMs), "pid_sample_ms", "poollogic", ConfigType::Int32,
                                              &pidSampleMs_, ConfigPersistence::Persistent, 0};

    // CFGDOC: {"label":"Délai démarrage pression (s)","help":"Temps d'attente avant vérification pression après démarrage.","unit":"s"}
    ConfigVariable<uint8_t,0> psiDelayVar_{NVS_KEY(NvsKeys::PoolLogic::PsiDelay), "psi_start_dly_s", "poollogic", ConfigType::UInt8,
                                           &psiStartupDelaySec_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Délai PID après filtration (min)","help":"Délai avant activation des PID après début filtration.","unit":"min"}
    ConfigVariable<uint8_t,0> delayPidsVar_{NVS_KEY(NvsKeys::PoolLogic::DelayPids), "dly_pid_min", "poollogic", ConfigType::UInt8,
                                            &delayPidsMin_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Délai electrolyse (min)","help":"Délai avant autorisation de l'electrolyse après filtration.","unit":"min"}
    ConfigVariable<uint8_t,0> delayElectroVar_{NVS_KEY(NvsKeys::PoolLogic::DelayElectro), "dly_electro_min", "poollogic", ConfigType::UInt8,
                                               &delayElectroMin_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Délai robot (min)","help":"Délai avant lancement automatique du robot.","unit":"min"}
    ConfigVariable<uint8_t,0> robotDelayVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDelay), "robot_delay_min", "poollogic", ConfigType::UInt8,
                                             &robotDelayMin_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Durée robot (min)","help":"Durée de fonctionnement du robot.","unit":"min"}
    ConfigVariable<uint8_t,0> robotDurationVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDuration), "robot_dur_min", "poollogic", ConfigType::UInt8,
                                                &robotDurationMin_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Temps mini remplissage (s)","help":"Durée minimale de marche de la pompe de remplissage.","unit":"s"}
    ConfigVariable<uint8_t,0> fillingMinOnVar_{NVS_KEY(NvsKeys::PoolLogic::FillingMinOn), "fill_min_on_s", "poollogic", ConfigType::UInt8,
                                               &fillingMinOnSec_, ConfigPersistence::Persistent, 0};

    // CFGDOC: {"label":"Slot filtration","help":"Numéro de slot PDM pilote pour la filtration."}
    ConfigVariable<uint8_t,0> filtrationDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationSlot), "filtr_slot", "poollogic", ConfigType::UInt8,
                                                   &filtrationDeviceSlot_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Slot electrolyse","help":"Numéro de slot PDM associé à l'electrolyseur."}
    ConfigVariable<uint8_t,0> swgDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::SwgSlot), "swg_slot", "poollogic", ConfigType::UInt8,
                                            &swgDeviceSlot_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Slot robot","help":"Numéro de slot PDM associé au robot."}
    ConfigVariable<uint8_t,0> robotDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::RobotSlot), "robot_slot", "poollogic", ConfigType::UInt8,
                                              &robotDeviceSlot_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Slot remplissage","help":"Numéro de slot PDM associé au remplissage."}
    ConfigVariable<uint8_t,0> fillingDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::FillingSlot), "fill_slot", "poollogic", ConfigType::UInt8,
                                                &fillingDeviceSlot_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Slot pompe pH","help":"Numéro de slot PDM associé à la pompe pH."}
    ConfigVariable<uint8_t,0> phPumpDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::PhPumpSlot), "ph_pump_slot", "poollogic", ConfigType::UInt8,
                                               &phPumpDeviceSlot_, ConfigPersistence::Persistent, 0};
    // CFGDOC: {"label":"Slot pompe chlore liquide","help":"Numéro de slot PDM associé à la pompe ORP/chlore liquide."}
    ConfigVariable<uint8_t,0> orpPumpDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::OrpPumpSlot), "orp_pump_slot", "poollogic", ConfigType::UInt8,
                                                &orpPumpDeviceSlot_, ConfigPersistence::Persistent, 0};

    // Services and adapters
    ConfigStore* cfgStore_ = nullptr;
    EventBus* eventBus_ = nullptr;
    const TimeSchedulerService* schedSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const PoolDeviceService* poolSvc_ = nullptr;
    const HAService* haSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;

    // Lifecycle
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void normalizeDeviceSlots_();
    void logDeviceSlotConfig_() const;
    void logDeviceSlotBinding_(const char* role, uint8_t slot, int8_t expectedType) const;

    // Scheduler
    void ensureDailySlot_();
    bool applyFiltrationWindowSlot_(uint8_t startHour, uint8_t stopHour);
    bool computeFiltrationWindow_(float waterTemp, uint8_t& startHourOut, uint8_t& stopHourOut, uint8_t& durationOut);
    bool recalcAndApplyFiltrationWindow_(uint8_t* startHourOut = nullptr,
                                         uint8_t* stopHourOut = nullptr,
                                         uint8_t* durationOut = nullptr);

    // Control
    static AlarmCondState condPsiLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPsiHighStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPhTankLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condChlorineTankLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condWaterLevelLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPhPumpMaxUptimeStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condChlorinePumpMaxUptimeStatic_(void* ctx, uint32_t nowMs);
    AlarmCondState condPumpMaxUptime_(uint8_t deviceSlot) const;
    bool readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const;
    bool writeDeviceDesired_(uint8_t deviceSlot, bool on);
    void syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut);
    uint32_t stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const;
    bool loadAnalogSensor_(IoId ioId, float& out) const;
    bool loadDigitalSensor_(IoId ioId, bool& out) const;
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

    // Runtime
    MqttBuildResult buildCfgBase_(MqttBuildContext& buildCtx);

    // Commands
    static bool cmdFiltrationWriteStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFiltrationRecalcStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdAutoModeSetStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdFiltrationWrite_(const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdFiltrationRecalc_(const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdAutoModeSet_(const CommandRequest& req, char* reply, size_t replyLen);
};
