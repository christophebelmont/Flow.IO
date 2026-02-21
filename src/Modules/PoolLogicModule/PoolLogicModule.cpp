/**
 * @file PoolLogicModule.cpp
 * @brief Implementation file.
 */

#include "PoolLogicModule.h"
#include "Modules/PoolLogicModule/FiltrationWindow.h"
#include "Core/MqttTopics.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"
#include "Core/CommandRegistry.h"

#include <Arduino.h>
#include <cstring>
#include <ArduinoJson.h>
#include <stdlib.h>
#include <cmath>

#define LOG_TAG "PoolLogc"
#include "Core/ModuleLog.h"

static bool parseCmdArgsObject_(const CommandRequest& req, JsonObjectConst& outObj)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdPoolDeviceBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    const char* json = req.args ? req.args : req.json;
    if (!json || json[0] == '\0') return false;

    const DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonObject>()) {
        outObj = doc.as<JsonObjectConst>();
        return true;
    }

    if (req.json && req.json[0] != '\0' && req.args != req.json) {
        doc.clear();
        const DeserializationError rootErr = deserializeJson(doc, req.json);
        if (rootErr || !doc.is<JsonObjectConst>()) return false;
        JsonVariantConst argsVar = doc["args"];
        if (argsVar.is<JsonObjectConst>()) {
            outObj = argsVar.as<JsonObjectConst>();
            return true;
        }
    }

    return false;
}

static bool parseBoolValue_(JsonVariantConst value, bool& out)
{
    if (value.is<bool>()) {
        out = value.as<bool>();
        return true;
    }
    if (value.is<int32_t>() || value.is<uint32_t>() || value.is<float>()) {
        out = (value.as<float>() != 0.0f);
        return true;
    }
    if (value.is<const char*>()) {
        const char* s = value.as<const char*>();
        if (!s) s = "0";
        if (strcmp(s, "true") == 0) out = true;
        else if (strcmp(s, "false") == 0) out = false;
        else out = (atoi(s) != 0);
        return true;
    }
    return false;
}

static void writeCmdError_(char* reply, size_t replyLen, const char* where, ErrorCode code)
{
    if (!writeErrorJson(reply, replyLen, code, where)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}

void PoolLogicModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::PoolLogic;
    constexpr uint16_t kCfgBranchMode = (uint16_t)ConfigBranchId::PoolLogicMode;
    constexpr uint16_t kCfgBranchFiltration = (uint16_t)ConfigBranchId::PoolLogicFiltration;
    constexpr uint16_t kCfgBranchSensors = (uint16_t)ConfigBranchId::PoolLogicSensors;
    constexpr uint16_t kCfgBranchPid = (uint16_t)ConfigBranchId::PoolLogicPid;
    constexpr uint16_t kCfgBranchDelay = (uint16_t)ConfigBranchId::PoolLogicDelay;
    constexpr uint16_t kCfgBranchDevice = (uint16_t)ConfigBranchId::PoolLogicDevice;
    static constexpr const char* kCfgModuleMode = "poollogic/mode";
    static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
    static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
    static constexpr const char* kCfgModulePid = "poollogic/pid";
    static constexpr const char* kCfgModuleDelay = "poollogic/delay";
    static constexpr const char* kCfgModuleDevice = "poollogic/device";
    cfgStore_ = &cfg;

    enabledVar_.moduleName = kCfgModuleMode;
    autoModeVar_.moduleName = kCfgModuleMode;
    winterModeVar_.moduleName = kCfgModuleMode;
    phAutoModeVar_.moduleName = kCfgModuleMode;
    orpAutoModeVar_.moduleName = kCfgModuleMode;
    electrolyseModeVar_.moduleName = kCfgModuleMode;
    electroRunModeVar_.moduleName = kCfgModuleMode;

    tempLowVar_.moduleName = kCfgModuleFiltration;
    tempSetpointVar_.moduleName = kCfgModuleFiltration;
    startMinVar_.moduleName = kCfgModuleFiltration;
    stopMaxVar_.moduleName = kCfgModuleFiltration;
    calcStartVar_.moduleName = kCfgModuleFiltration;
    calcStopVar_.moduleName = kCfgModuleFiltration;

    phIdVar_.moduleName = kCfgModuleSensors;
    orpIdVar_.moduleName = kCfgModuleSensors;
    psiIdVar_.moduleName = kCfgModuleSensors;
    waterTempIdVar_.moduleName = kCfgModuleSensors;
    airTempIdVar_.moduleName = kCfgModuleSensors;
    levelIdVar_.moduleName = kCfgModuleSensors;

    psiLowVar_.moduleName = kCfgModulePid;
    psiHighVar_.moduleName = kCfgModulePid;
    winterStartVar_.moduleName = kCfgModulePid;
    freezeHoldVar_.moduleName = kCfgModulePid;
    secureElectroVar_.moduleName = kCfgModulePid;
    phSetpointVar_.moduleName = kCfgModulePid;
    orpSetpointVar_.moduleName = kCfgModulePid;
    phKpVar_.moduleName = kCfgModulePid;
    phKiVar_.moduleName = kCfgModulePid;
    phKdVar_.moduleName = kCfgModulePid;
    orpKpVar_.moduleName = kCfgModulePid;
    orpKiVar_.moduleName = kCfgModulePid;
    orpKdVar_.moduleName = kCfgModulePid;
    phWindowMsVar_.moduleName = kCfgModulePid;
    orpWindowMsVar_.moduleName = kCfgModulePid;
    pidMinOnMsVar_.moduleName = kCfgModulePid;
    pidSampleMsVar_.moduleName = kCfgModulePid;

    psiDelayVar_.moduleName = kCfgModuleDelay;
    delayPidsVar_.moduleName = kCfgModuleDelay;
    delayElectroVar_.moduleName = kCfgModuleDelay;
    robotDelayVar_.moduleName = kCfgModuleDelay;
    robotDurationVar_.moduleName = kCfgModuleDelay;
    fillingMinOnVar_.moduleName = kCfgModuleDelay;

    filtrationDeviceVar_.moduleName = kCfgModuleDevice;
    swgDeviceVar_.moduleName = kCfgModuleDevice;
    robotDeviceVar_.moduleName = kCfgModuleDevice;
    fillingDeviceVar_.moduleName = kCfgModuleDevice;

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchMode);

    cfg.registerVar(autoModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(winterModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(phAutoModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(orpAutoModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(electrolyseModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(electroRunModeVar_, kCfgModuleId, kCfgBranchMode);

    cfg.registerVar(tempLowVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(tempSetpointVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(startMinVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(stopMaxVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(calcStartVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(calcStopVar_, kCfgModuleId, kCfgBranchFiltration);

    cfg.registerVar(phIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(orpIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(psiIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(waterTempIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(airTempIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(levelIdVar_, kCfgModuleId, kCfgBranchSensors);

    cfg.registerVar(psiLowVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(psiHighVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(winterStartVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(freezeHoldVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(secureElectroVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(phSetpointVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(orpSetpointVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(phKpVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(phKiVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(phKdVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(orpKpVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(orpKiVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(orpKdVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(phWindowMsVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(orpWindowMsVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(pidMinOnMsVar_, kCfgModuleId, kCfgBranchPid);
    cfg.registerVar(pidSampleMsVar_, kCfgModuleId, kCfgBranchPid);

    cfg.registerVar(psiDelayVar_, kCfgModuleId, kCfgBranchDelay);
    cfg.registerVar(delayPidsVar_, kCfgModuleId, kCfgBranchDelay);
    cfg.registerVar(delayElectroVar_, kCfgModuleId, kCfgBranchDelay);
    cfg.registerVar(robotDelayVar_, kCfgModuleId, kCfgBranchDelay);
    cfg.registerVar(robotDurationVar_, kCfgModuleId, kCfgBranchDelay);
    cfg.registerVar(fillingMinOnVar_, kCfgModuleId, kCfgBranchDelay);

    cfg.registerVar(filtrationDeviceVar_, kCfgModuleId, kCfgBranchDevice);
    cfg.registerVar(swgDeviceVar_, kCfgModuleId, kCfgBranchDevice);
    cfg.registerVar(robotDeviceVar_, kCfgModuleId, kCfgBranchDevice);
    cfg.registerVar(fillingDeviceVar_, kCfgModuleId, kCfgBranchDevice);

    logHub_ = services.get<LogHubService>("loghub");
    const EventBusService* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    schedSvc_ = services.get<TimeSchedulerService>("time.scheduler");
    ioSvc_ = services.get<IOServiceV2>("io");
    poolSvc_ = services.get<PoolDeviceService>("pooldev");
    haSvc_ = services.get<HAService>("ha");
    cmdSvc_ = services.get<CommandService>("cmd");
    alarmSvc_ = services.get<AlarmService>("alarms");
    if (!ioSvc_) {
        LOGW("PoolLogic waiting for IOServiceV2");
    }
    if (!poolSvc_) {
        LOGW("PoolLogic waiting for PoolDeviceService");
    }
    if (haSvc_ && haSvc_->addSwitch) {
        const HASwitchEntry autoModeSwitch{
            "poollogic",
            "pool_auto_mode",
            "Pool Auto-regulation",
            "cfg/poollogic/mode",
            "{% if value_json.auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/mode\\\":{\\\"auto_mode\\\":true}}",
            "{\\\"poollogic/mode\\\":{\\\"auto_mode\\\":false}}",
            "mdi:calendar-clock",
            "config"
        };
        const HASwitchEntry winterModeSwitch{
            "poollogic",
            "pool_winter_mode",
            "Winter Mode",
            "cfg/poollogic/mode",
            "{% if value_json.winter_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/mode\\\":{\\\"winter_mode\\\":true}}",
            "{\\\"poollogic/mode\\\":{\\\"winter_mode\\\":false}}",
            "mdi:snowflake",
            "config"
        };
        const HASwitchEntry phAutoModeSwitch{
            "poollogic",
            "pool_ph_auto_mode",
            "pH Auto-regulation",
            "cfg/poollogic/mode",
            "{% if value_json.ph_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/mode\\\":{\\\"ph_auto_mode\\\":true}}",
            "{\\\"poollogic/mode\\\":{\\\"ph_auto_mode\\\":false}}",
            "mdi:beaker-check-outline",
            "config"
        };
        const HASwitchEntry orpAutoModeSwitch{
            "poollogic",
            "pool_orp_auto_mode",
            "Orp Auto-regulation",
            "cfg/poollogic/mode",
            "{% if value_json.orp_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/mode\\\":{\\\"orp_auto_mode\\\":true}}",
            "{\\\"poollogic/mode\\\":{\\\"orp_auto_mode\\\":false}}",
            "mdi:water-check-outline",
            "config"
        };
        (void)haSvc_->addSwitch(haSvc_->ctx, &autoModeSwitch);
        (void)haSvc_->addSwitch(haSvc_->ctx, &winterModeSwitch);
        (void)haSvc_->addSwitch(haSvc_->ctx, &phAutoModeSwitch);
        (void)haSvc_->addSwitch(haSvc_->ctx, &orpAutoModeSwitch);
    }
    if (haSvc_ && haSvc_->addSensor) {
        const HASensorEntry filtrationStart{
            "poollogic",
            "calculated_filtration_start",
            "Calculated Filtration Start",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_start_clc | int(0) }}",
            nullptr,
            "mdi:clock-start",
            "h"
        };
        const HASensorEntry filtrationStop{
            "poollogic",
            "calculated_filtration_stop",
            "Calculated Filtration Stop",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_stop_clc | int(0) }}",
            nullptr,
            "mdi:clock-end",
            "h"
        };
        (void)haSvc_->addSensor(haSvc_->ctx, &filtrationStart);
        (void)haSvc_->addSensor(haSvc_->ctx, &filtrationStop);
    }
    if (haSvc_ && haSvc_->addNumber) {
        const HANumberEntry delayPidsMin{
            "poollogic",
            "delay_pids_min",
            "Delay PIDs",
            "cfg/poollogic/delay",
            "{{ value_json.dly_pid_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/delay\\\":{\\\"dly_pid_min\\\":{{ value | int(0) }}}}",
            0.0f,
            180.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-sand",
            "min"
        };
        const HANumberEntry phSetpoint{
            "poollogic",
            "ph_setpoint",
            "pH Setpoint",
            "cfg/poollogic/pid",
            "{{ value_json.ph_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/pid\\\":{\\\"ph_setpoint\\\":{{ value | float(0) }}}}",
            6.0f,
            8.0f,
            0.01f,
            "slider",
            "config",
            "mdi:beaker-outline",
            nullptr
        };
        const HANumberEntry orpSetpoint{
            "poollogic",
            "orp_setpoint",
            "Orp Setpoint",
            "cfg/poollogic/pid",
            "{{ value_json.orp_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/pid\\\":{\\\"orp_setpoint\\\":{{ value | float(0) }}}}",
            300.0f,
            900.0f,
            1.0f,
            "slider",
            "config",
            "mdi:water-outline",
            "mV"
        };
        const HANumberEntry phWindowMin{
            "poollogic",
            "ph_pid_window_min",
            "pH PID Window Size",
            "cfg/poollogic/pid",
            "{{ ((value_json.ph_window_ms | float(0)) / 60000) | round(0) | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/pid\\\":{\\\"ph_window_ms\\\":{{ (value | float(0) * 60000) | round(0) | int(0) }}}}",
            1.0f,
            180.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timeline-clock-outline",
            "min"
        };
        const HANumberEntry orpWindowMin{
            "poollogic",
            "orp_pid_window_min",
            "Orp PID Window Size",
            "cfg/poollogic/pid",
            "{{ ((value_json.orp_window_ms | float(0)) / 60000) | round(0) | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/pid\\\":{\\\"orp_window_ms\\\":{{ (value | float(0) * 60000) | round(0) | int(0) }}}}",
            1.0f,
            180.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timeline-clock-outline",
            "min"
        };
        const HANumberEntry psiLowThreshold{
            "poollogic",
            "psi_low_threshold",
            "PSI Low Threshold",
            "cfg/poollogic/pid",
            "{{ value_json.psi_low_th | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/pid\\\":{\\\"psi_low_th\\\":{{ value | float(0) }}}}",
            0.0f,
            5.0f,
            0.01f,
            "slider",
            "config",
            "mdi:gauge-low",
            "bar"
        };
        const HANumberEntry psiHighThreshold{
            "poollogic",
            "psi_high_threshold",
            "PSI High Threshold",
            "cfg/poollogic/pid",
            "{{ value_json.psi_high_th | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/pid\\\":{\\\"psi_high_th\\\":{{ value | float(0) }}}}",
            0.0f,
            5.0f,
            0.01f,
            "slider",
            "config",
            "mdi:gauge-full",
            "bar"
        };
        (void)haSvc_->addNumber(haSvc_->ctx, &delayPidsMin);
        (void)haSvc_->addNumber(haSvc_->ctx, &phSetpoint);
        (void)haSvc_->addNumber(haSvc_->ctx, &orpSetpoint);
        (void)haSvc_->addNumber(haSvc_->ctx, &phWindowMin);
        (void)haSvc_->addNumber(haSvc_->ctx, &orpWindowMin);
        (void)haSvc_->addNumber(haSvc_->ctx, &psiLowThreshold);
        (void)haSvc_->addNumber(haSvc_->ctx, &psiHighThreshold);
    }
    if (haSvc_ && haSvc_->addButton) {
        const HAButtonEntry filtrationRecalc{
            "poollogic",
            "filtration_recalc",
            "Recalculate Filtration Window",
            MqttTopics::SuffixCmd,
            "{\\\"cmd\\\":\\\"poollogic.filtration.recalc\\\"}",
            "config",
            "mdi:refresh"
        };
        (void)haSvc_->addButton(haSvc_->ctx, &filtrationRecalc);
    }
    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "poollogic.filtration.write", &PoolLogicModule::cmdFiltrationWriteStatic_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "poollogic.filtration.recalc", &PoolLogicModule::cmdFiltrationRecalcStatic_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "poollogic.auto_mode.set", &PoolLogicModule::cmdAutoModeSetStatic_, this);
    }
    if (alarmSvc_ && alarmSvc_->registerAlarm) {
        const AlarmRegistration psiLowAlarm{
            AlarmId::PoolPsiLow,
            AlarmSeverity::Alarm,
            true,
            2000,
            1000,
            60000,
            "psi_low",
            "Low pressure",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &psiLowAlarm, &PoolLogicModule::condPsiLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPsiLow");
        }

        const AlarmRegistration psiHighAlarm{
            AlarmId::PoolPsiHigh,
            AlarmSeverity::Critical,
            true,
            0,
            1000,
            60000,
            "psi_high",
            "High pressure",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &psiHighAlarm, &PoolLogicModule::condPsiHighStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPsiHigh");
        }
    } else {
        LOGW("PoolLogic running without alarm service");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolLogicModule::onEventStatic_, this);
    }

    if (!enabled_) {
        LOGI("PoolLogic disabled");
        return;
    }

    LOGI("PoolLogic ready");
    (void)cfgStore_;
    (void)logHub_;
}

void PoolLogicModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    if (!enabled_) return;

    ensureDailySlot_();

    if (schedSvc_ && schedSvc_->isActive) {
        filtrationWindowActive_ = schedSvc_->isActive(schedSvc_->ctx, SLOT_FILTR_WINDOW);
    }

    // Trigger one recompute on startup, after persisted config and scheduler blob
    // are fully loaded.
    portENTER_CRITICAL(&pendingMux_);
    pendingDailyRecalc_ = true;
    portEXIT_CRITICAL(&pendingMux_);
}

uint8_t PoolLogicModule::runtimeSnapshotCount() const
{
    return 2;
}

const char* PoolLogicModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    if (idx == 0) return "rt/poollogic/ph";
    if (idx == 1) return "rt/poollogic/orp";
    return nullptr;
}

bool PoolLogicModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;

    const uint32_t nowMs = millis();
    const bool isPh = (idx == 0);
    const bool isOrp = (idx == 1);
    if (!isPh && !isOrp) return false;

    const TemporalPidState& st = isPh ? phPidState_ : orpPidState_;
    const DeviceFsm& pumpFsm = isPh ? phPumpFsm_ : orpPumpFsm_;
    const float kp = isPh ? phKp_ : orpKp_;
    const float ki = isPh ? phKi_ : orpKi_;
    const float kd = isPh ? phKd_ : orpKd_;
    const int32_t windowMsCfg = isPh ? phWindowMs_ : orpWindowMs_;
    const uint32_t windowMs = (windowMsCfg > 1000) ? (uint32_t)windowMsCfg : 1000U;

    uint32_t elapsedMs = 0;
    if (st.initialized) {
        const uint32_t rawElapsed = nowMs - st.windowStartMs;
        elapsedMs = (rawElapsed < windowMs) ? rawElapsed : windowMs;
    }

    const bool regulationEnabled = isPh ? phPidEnabled_ : orpPidEnabled_;
    int wrote = 0;
    if (st.sampleValid) {
        wrote = snprintf(
            out, len,
            "{\"id\":\"%s\",\"input\":%.3f,\"setpoint\":%.3f,\"error\":%.3f,"
            "\"enabled\":%s,\"demand\":%s,\"actual\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"window_ms\":%ld,\"sample_ms\":%ld,\"min_on_ms\":%ld,"
            "\"output_on_ms\":%lu,\"window_elapsed_ms\":%lu,\"compute_ts\":%lu,"
            "\"electrolyse_mode\":%s,\"ts\":%lu}",
            isPh ? "ph" : "orp",
            (double)st.sampleInput,
            (double)st.sampleSetpoint,
            (double)st.sampleError,
            regulationEnabled ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)kp,
            (double)ki,
            (double)kd,
            (long)windowMsCfg,
            (long)pidSampleMs_,
            (long)pidMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            (unsigned long)st.sampleTsMs,
            electrolyseMode_ ? "true" : "false",
            (unsigned long)nowMs
        );
    } else {
        wrote = snprintf(
            out, len,
            "{\"id\":\"%s\",\"input\":null,\"setpoint\":%.3f,\"error\":null,"
            "\"enabled\":%s,\"demand\":%s,\"actual\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"window_ms\":%ld,\"sample_ms\":%ld,\"min_on_ms\":%ld,"
            "\"output_on_ms\":%lu,\"window_elapsed_ms\":%lu,\"compute_ts\":0,"
            "\"electrolyse_mode\":%s,\"ts\":%lu}",
            isPh ? "ph" : "orp",
            (double)(isPh ? phSetpoint_ : orpSetpoint_),
            regulationEnabled ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)kp,
            (double)ki,
            (double)kd,
            (long)windowMsCfg,
            (long)pidSampleMs_,
            (long)pidMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            electrolyseMode_ ? "true" : "false",
            (unsigned long)nowMs
        );
    }
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = st.runtimeTsMs ? st.runtimeTsMs : 1U;
    return true;
}

AlarmCondState PoolLogicModule::condPsiLowStatic_(void* ctx, uint32_t nowMs)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;
    const uint32_t runSec = self->stateUptimeSec_(self->filtrationFsm_, nowMs);
    if (runSec <= self->psiStartupDelaySec_) return AlarmCondState::False;

    float psi = 0.0f;
    if (!self->loadAnalogSensor_(self->psiIoId_, psi)) {
        return AlarmCondState::Unknown;
    }

    return (psi < self->psiLowThreshold_) ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condPsiHighStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;

    float psi = 0.0f;
    if (!self->loadAnalogSensor_(self->psiIoId_, psi)) {
        return AlarmCondState::Unknown;
    }

    return (psi > self->psiHighThreshold_) ? AlarmCondState::True : AlarmCondState::False;
}

bool PoolLogicModule::cmdFiltrationWriteStatic_(void* userCtx,
                                                const CommandRequest& req,
                                                char* reply,
                                                size_t replyLen)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(userCtx);
    if (!self) return false;
    return self->cmdFiltrationWrite_(req, reply, replyLen);
}

bool PoolLogicModule::cmdAutoModeSetStatic_(void* userCtx,
                                            const CommandRequest& req,
                                            char* reply,
                                            size_t replyLen)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(userCtx);
    if (!self) return false;
    return self->cmdAutoModeSet_(req, reply, replyLen);
}

bool PoolLogicModule::cmdFiltrationRecalcStatic_(void* userCtx,
                                                 const CommandRequest& req,
                                                 char* reply,
                                                 size_t replyLen)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(userCtx);
    if (!self) return false;
    return self->cmdFiltrationRecalc_(req, reply, replyLen);
}

bool PoolLogicModule::cmdFiltrationWrite_(const CommandRequest& req, char* reply, size_t replyLen)
{
    if (!cfgStore_ || !poolSvc_ || !poolSvc_->writeDesired) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::NotReady);
        return false;
    }

    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::MissingArgs);
        return false;
    }
    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::MissingValue);
        return false;
    }

    bool requested = false;
    if (!parseBoolValue_(args["value"], requested)) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::MissingValue);
        return false;
    }

    (void)cfgStore_->set(autoModeVar_, false);
    autoMode_ = false;

    const PoolDeviceSvcStatus st = poolSvc_->writeDesired(poolSvc_->ctx, filtrationDeviceSlot_, requested ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        ErrorCode code = ErrorCode::Failed;
        if (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) code = ErrorCode::UnknownSlot;
        else if (st == POOLDEV_SVC_ERR_NOT_READY) code = ErrorCode::NotReady;
        else if (st == POOLDEV_SVC_ERR_DISABLED) code = ErrorCode::Disabled;
        else if (st == POOLDEV_SVC_ERR_INTERLOCK) code = ErrorCode::InterlockBlocked;
        else if (st == POOLDEV_SVC_ERR_IO) code = ErrorCode::IoError;
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", code);
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u,\"value\":%s,\"auto_mode\":false}",
             (unsigned)filtrationDeviceSlot_,
             requested ? "true" : "false");
    return true;
}

bool PoolLogicModule::cmdFiltrationRecalc_(const CommandRequest&, char* reply, size_t replyLen)
{
    if (!enabled_) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.recalc", ErrorCode::Disabled);
        return false;
    }

    // Match scheduler-trigger behavior: queue the recalc and let loop() own execution.
    portENTER_CRITICAL(&pendingMux_);
    pendingDailyRecalc_ = true;
    portEXIT_CRITICAL(&pendingMux_);

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true}");
    return true;
}

bool PoolLogicModule::cmdAutoModeSet_(const CommandRequest& req, char* reply, size_t replyLen)
{
    if (!cfgStore_) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::NotReady);
        return false;
    }

    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::MissingArgs);
        return false;
    }
    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::MissingValue);
        return false;
    }

    bool requested = false;
    if (!parseBoolValue_(args["value"], requested)) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::MissingValue);
        return false;
    }

    (void)cfgStore_->set(autoModeVar_, requested);
    autoMode_ = requested;

    snprintf(reply, replyLen, "{\"ok\":true,\"auto_mode\":%s}", requested ? "true" : "false");
    return true;
}

void PoolLogicModule::loop()
{
    if (!enabled_) {
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }
    if (!startupReady_) {
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    bool doRecalc = false;
    bool doDayReset = false;
    portENTER_CRITICAL(&pendingMux_);
    doRecalc = pendingDailyRecalc_;
    pendingDailyRecalc_ = false;
    doDayReset = pendingDayReset_;
    pendingDayReset_ = false;
    portEXIT_CRITICAL(&pendingMux_);

    if (doRecalc) {
        (void)recalcAndApplyFiltrationWindow_();
    }

    if (doDayReset) {
        cleaningDone_ = false;
        LOGI("Daily reset: cleaning_done=false");
    }

    runControlLoop_(millis());
    vTaskDelay(pdMS_TO_TICKS(200));
}

void PoolLogicModule::onEventStatic_(const Event& e, void* user)
{
    if (!user) return;
    static_cast<PoolLogicModule*>(user)->onEvent_(e);
}

void PoolLogicModule::onEvent_(const Event& e)
{
    if (!enabled_) return;
    if (e.id != EventId::SchedulerEventTriggered) return;
    if (!e.payload || e.len < sizeof(SchedulerEventTriggeredPayload)) return;

    const SchedulerEventTriggeredPayload* p = (const SchedulerEventTriggeredPayload*)e.payload;
    const SchedulerEdge edge = (SchedulerEdge)p->edge;

    if (p->eventId == POOLLOGIC_EVENT_DAILY_RECALC && edge == SchedulerEdge::Trigger) {
        portENTER_CRITICAL(&pendingMux_);
        pendingDailyRecalc_ = true;
        portEXIT_CRITICAL(&pendingMux_);
        return;
    }

    if (p->eventId == TIME_EVENT_SYS_DAY_START && edge == SchedulerEdge::Trigger) {
        portENTER_CRITICAL(&pendingMux_);
        pendingDayReset_ = true;
        portEXIT_CRITICAL(&pendingMux_);
        return;
    }

    if (p->eventId == POOLLOGIC_EVENT_FILTRATION_WINDOW) {
        if (edge == SchedulerEdge::Start) {
            portENTER_CRITICAL(&pendingMux_);
            filtrationWindowActive_ = true;
            portEXIT_CRITICAL(&pendingMux_);
        } else if (edge == SchedulerEdge::Stop) {
            portENTER_CRITICAL(&pendingMux_);
            filtrationWindowActive_ = false;
            portEXIT_CRITICAL(&pendingMux_);
        }
    }
}

void PoolLogicModule::ensureDailySlot_()
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("time.scheduler service unavailable");
        return;
    }

    TimeSchedulerSlot recalc{};
    recalc.slot = SLOT_DAILY_RECALC;
    recalc.eventId = POOLLOGIC_EVENT_DAILY_RECALC;
    recalc.enabled = true;
    recalc.hasEnd = false;
    recalc.replayStartOnBoot = false;
    recalc.mode = TimeSchedulerMode::RecurringClock;
    recalc.weekdayMask = TIME_WEEKDAY_ALL;
    recalc.startHour = PoolDefaults::FiltrationPivotHour;
    recalc.startMinute = 0;
    recalc.endHour = 0;
    recalc.endMinute = 0;
    recalc.startEpochSec = 0;
    recalc.endEpochSec = 0;
    strncpy(recalc.label, "poollogic_daily_recalc", sizeof(recalc.label) - 1);
    recalc.label[sizeof(recalc.label) - 1] = '\0';

    if (!schedSvc_->setSlot(schedSvc_->ctx, &recalc)) {
        LOGW("Failed to set scheduler slot %u", (unsigned)SLOT_DAILY_RECALC);
    }
}

bool PoolLogicModule::computeFiltrationWindow_(float waterTemp, uint8_t& startHourOut, uint8_t& stopHourOut, uint8_t& durationOut)
{
    FiltrationWindowInput in{};
    in.waterTemp = waterTemp;
    in.lowThreshold = waterTempLowThreshold_;
    in.setpoint = waterTempSetpoint_;
    in.startMinHour = filtrationStartMin_;
    in.stopMaxHour = filtrationStopMax_;

    FiltrationWindowOutput out{};
    if (!computeFiltrationWindowDeterministic(in, out)) return false;
    startHourOut = out.startHour;
    stopHourOut = out.stopHour;
    durationOut = out.durationHours;
    return true;
}

bool PoolLogicModule::recalcAndApplyFiltrationWindow_(uint8_t* startHourOut,
                                                      uint8_t* stopHourOut,
                                                      uint8_t* durationOut)
{
    if (!ioSvc_ || !ioSvc_->readAnalog) {
        LOGW("No IOServiceV2 available for water temperature");
        return false;
    }
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("No time.scheduler service available");
        return false;
    }

    float waterTemp = 0.0f;
    if (!loadAnalogSensor_(waterTempIoId_, waterTemp)) {
        LOGW("Water temperature unavailable on ioId=%u", (unsigned)waterTempIoId_);
        return false;
    }

    uint8_t startHour = 0;
    uint8_t stopHour = 0;
    uint8_t duration = 0;
    if (!computeFiltrationWindow_(waterTemp, startHour, stopHour, duration)) {
        LOGW("Invalid water temperature value");
        return false;
    }

    TimeSchedulerSlot window{};
    window.slot = SLOT_FILTR_WINDOW;
    window.eventId = POOLLOGIC_EVENT_FILTRATION_WINDOW;
    window.enabled = true;
    window.hasEnd = true;
    window.replayStartOnBoot = true;
    window.mode = TimeSchedulerMode::RecurringClock;
    window.weekdayMask = TIME_WEEKDAY_ALL;
    window.startHour = startHour;
    window.startMinute = 0;
    window.endHour = stopHour;
    window.endMinute = 0;
    window.startEpochSec = 0;
    window.endEpochSec = 0;
    strncpy(window.label, "poollogic_filtration", sizeof(window.label) - 1);
    window.label[sizeof(window.label) - 1] = '\0';

    if (!schedSvc_->setSlot(schedSvc_->ctx, &window)) {
        LOGW("Failed to set filtration window slot=%u", (unsigned)SLOT_FILTR_WINDOW);
        return false;
    }

    if (cfgStore_) {
        (void)cfgStore_->set(calcStartVar_, startHour);
        (void)cfgStore_->set(calcStopVar_, stopHour);
    } else {
        filtrationCalcStart_ = startHour;
        filtrationCalcStop_ = stopHour;
    }
    if (startHourOut) *startHourOut = startHour;
    if (stopHourOut) *stopHourOut = stopHour;
    if (durationOut) *durationOut = duration;

    LOGI("Filtration duration=%uh water=%.2fC start=%uh stop=%uh",
         (unsigned)duration,
         (double)waterTemp,
         (unsigned)startHour,
         (unsigned)stopHour);
    return true;
}

bool PoolLogicModule::readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const
{
    if (!poolSvc_ || !poolSvc_->readActualOn) return false;
    uint8_t on = 0;
    if (poolSvc_->readActualOn(poolSvc_->ctx, deviceSlot, &on, nullptr) != POOLDEV_SVC_OK) return false;
    onOut = (on != 0U);
    return true;
}

bool PoolLogicModule::writeDeviceDesired_(uint8_t deviceSlot, bool on)
{
    if (!poolSvc_ || !poolSvc_->writeDesired) return false;
    const PoolDeviceSvcStatus st = poolSvc_->writeDesired(poolSvc_->ctx, deviceSlot, on ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        LOGW("pooldev.writeDesired failed slot=%u desired=%u st=%u",
             (unsigned)deviceSlot,
             on ? 1u : 0u,
             (unsigned)st);
        return false;
    }
    return true;
}

void PoolLogicModule::syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut)
{
    turnedOnOut = false;
    turnedOffOut = false;

    bool actualOn = false;
    if (!readDeviceActualOn_(deviceSlot, actualOn)) {
        return;
    }

    if (!fsm.known) {
        fsm.known = true;
        fsm.on = actualOn;
        fsm.stateSinceMs = nowMs;
        return;
    }

    if (fsm.on != actualOn) {
        turnedOnOut = (!fsm.on && actualOn);
        turnedOffOut = (fsm.on && !actualOn);
        fsm.on = actualOn;
        fsm.stateSinceMs = nowMs;
    }
}

uint32_t PoolLogicModule::stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const
{
    if (!fsm.known || !fsm.on) return 0;
    return (uint32_t)((nowMs - fsm.stateSinceMs) / 1000UL);
}

bool PoolLogicModule::loadAnalogSensor_(uint8_t ioId, float& out) const
{
    if (!ioSvc_ || !ioSvc_->readAnalog) return false;
    return ioSvc_->readAnalog(ioSvc_->ctx, (IoId)ioId, &out, nullptr, nullptr) == IO_OK;
}

bool PoolLogicModule::loadDigitalSensor_(uint8_t ioId, bool& out) const
{
    if (!ioSvc_ || !ioSvc_->readDigital) return false;
    uint8_t on = 0;
    if (ioSvc_->readDigital(ioSvc_->ctx, (IoId)ioId, &on, nullptr, nullptr) != IO_OK) return false;
    out = (on != 0U);
    return true;
}

void PoolLogicModule::resetTemporalPidState_(TemporalPidState& st, uint32_t nowMs)
{
    st.initialized = false;
    st.sampleValid = false;
    st.lastDemandOn = false;
    st.windowStartMs = nowMs;
    st.lastComputeMs = nowMs;
    st.sampleTsMs = 0;
    st.outputOnMs = 0;
    st.sampleInput = 0.0f;
    st.sampleSetpoint = 0.0f;
    st.sampleError = 0.0f;
    st.integral = 0.0f;
    st.prevError = 0.0f;
    st.lastError = 0.0f;
    st.runtimeTsMs = nowMs;
}

bool PoolLogicModule::stepTemporalPid_(TemporalPidState& st,
                                       float input,
                                       float setpoint,
                                       float kp,
                                       float ki,
                                       float kd,
                                       int32_t windowMsCfg,
                                       bool positiveWhenInputHigh,
                                       uint32_t nowMs,
                                       bool& demandOnOut,
                                       uint32_t& outputOnMsOut)
{
    const uint32_t windowMs = (windowMsCfg > 1000) ? (uint32_t)windowMsCfg : 1000U;
    const uint32_t sampleMs = (pidSampleMs_ > 100) ? (uint32_t)pidSampleMs_ : 100U;
    const uint32_t minOnMs = (pidMinOnMs_ > 0) ? (uint32_t)pidMinOnMs_ : 0U;

    if (!st.initialized) {
        st.initialized = true;
        st.windowStartMs = nowMs;
        st.lastComputeMs = nowMs;
        st.sampleValid = false;
        st.sampleTsMs = 0;
        st.sampleInput = 0.0f;
        st.sampleSetpoint = 0.0f;
        st.sampleError = 0.0f;
        st.integral = 0.0f;
        st.prevError = 0.0f;
        st.lastError = 0.0f;
        st.outputOnMs = 0;
        st.lastDemandOn = false;
        st.runtimeTsMs = nowMs;
    }

    while ((uint32_t)(nowMs - st.windowStartMs) >= windowMs) {
        st.windowStartMs += windowMs;
    }

    if ((uint32_t)(nowMs - st.lastComputeMs) >= sampleMs) {
        const uint32_t dtMs = nowMs - st.lastComputeMs;
        const float dtSec = (dtMs > 0U) ? ((float)dtMs / 1000.0f) : 0.0f;
        st.lastComputeMs = nowMs;

        const float error = positiveWhenInputHigh ? (input - setpoint) : (setpoint - input);
        st.lastError = error;

        float outputMs = 0.0f;
        if (error > 0.0f && std::isfinite(error)) {
            if (ki != 0.0f && dtSec > 0.0f) {
                st.integral += error * dtSec;
            } else {
                st.integral = 0.0f;
            }
            const float deriv = (dtSec > 0.0f) ? ((error - st.prevError) / dtSec) : 0.0f;
            outputMs = (kp * error) + (ki * st.integral) + (kd * deriv);
            if (!std::isfinite(outputMs) || outputMs < 0.0f) outputMs = 0.0f;
            if (outputMs > (float)windowMs) outputMs = (float)windowMs;
        } else {
            st.integral = 0.0f;
            outputMs = 0.0f;
        }
        st.prevError = error;
        st.sampleValid = true;
        st.sampleInput = input;
        st.sampleSetpoint = setpoint;
        st.sampleError = error;
        st.sampleTsMs = nowMs;
        st.runtimeTsMs = nowMs;

        uint32_t outMs = (uint32_t)(outputMs + 0.5f);
        if (outMs < minOnMs) outMs = 0U;
        if (outMs > windowMs) outMs = windowMs;
        st.outputOnMs = outMs;
    }

    const uint32_t elapsedMs = nowMs - st.windowStartMs;
    const bool demandOn = (st.outputOnMs > 0U) && (elapsedMs < st.outputOnMs);
    if (demandOn != st.lastDemandOn) {
        st.lastDemandOn = demandOn;
        st.runtimeTsMs = nowMs;
    }

    demandOnOut = demandOn;
    outputOnMsOut = st.outputOnMs;
    return true;
}

void PoolLogicModule::applyDeviceControl_(uint8_t deviceSlot,
                                          const char* label,
                                          DeviceFsm& fsm,
                                          bool desired,
                                          uint32_t nowMs)
{
    const bool desiredChanged = (desired != fsm.lastDesired);
    const bool needRetry = (fsm.known && (fsm.on != desired) && (uint32_t)(nowMs - fsm.lastCmdMs) >= 5000U);

    if (desiredChanged || needRetry) {
        if (writeDeviceDesired_(deviceSlot, desired)) {
            LOGI("%s %s", desired ? "Start" : "Stop", label ? label : "Pool Device");
        }
        fsm.lastCmdMs = nowMs;
    }

    fsm.lastDesired = desired;
}

void PoolLogicModule::runControlLoop_(uint32_t nowMs)
{
    bool filtrationStarted = false;
    bool filtrationStopped = false;
    bool robotStopped = false;
    bool unusedStart = false;
    bool unusedStop = false;

    syncDeviceState_(filtrationDeviceSlot_, filtrationFsm_, nowMs, filtrationStarted, filtrationStopped);
    syncDeviceState_(robotDeviceSlot_, robotFsm_, nowMs, unusedStart, robotStopped);
    syncDeviceState_(swgDeviceSlot_, swgFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(fillingDeviceSlot_, fillingFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(phPumpDeviceSlot_, phPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(orpPumpDeviceSlot_, orpPumpFsm_, nowMs, unusedStart, unusedStop);

    if (filtrationStarted) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
        resetTemporalPidState_(phPidState_, nowMs);
        resetTemporalPidState_(orpPidState_, nowMs);
    }
    if (filtrationStopped) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
        resetTemporalPidState_(phPidState_, nowMs);
        resetTemporalPidState_(orpPidState_, nowMs);
    }
    if (robotStopped) {
        cleaningDone_ = true;
    }

    float psi = 0.0f;
    float ph = 0.0f;
    float waterTemp = 0.0f;
    float airTemp = 0.0f;
    float orp = 0.0f;
    bool levelOk = true;

    const bool havePsi = loadAnalogSensor_(psiIoId_, psi);
    const bool havePh = loadAnalogSensor_(phIoId_, ph);
    const bool haveWaterTemp = loadAnalogSensor_(waterTempIoId_, waterTemp);
    const bool haveAirTemp = loadAnalogSensor_(airTempIoId_, airTemp);
    const bool haveOrp = loadAnalogSensor_(orpIoId_, orp);
    const bool haveLevel = loadDigitalSensor_(levelIoId_, levelOk);

    if (alarmSvc_ && alarmSvc_->isActive) {
        const bool psiLow = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolPsiLow);
        const bool psiHigh = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolPsiHigh);
        psiError_ = psiLow || psiHigh;
    } else if (filtrationFsm_.on && havePsi) {
        const uint32_t runSec = stateUptimeSec_(filtrationFsm_, nowMs);
        const bool underPressure = (runSec > psiStartupDelaySec_) && (psi < psiLowThreshold_);
        const bool overPressure = (psi > psiHighThreshold_);
        if ((underPressure || overPressure) && !psiError_) {
            psiError_ = true;
            LOGW("PSI error latched (psi=%.3f low=%.3f high=%.3f)",
                 (double)psi,
                 (double)psiLowThreshold_,
                 (double)psiHighThreshold_);
        }
    }

    if (filtrationFsm_.on && !winterMode_) {
        const uint32_t runMin = stateUptimeSec_(filtrationFsm_, nowMs) / 60U;

        if (phAutoMode_ && !phPidEnabled_ && runMin >= delayPidsMin_) {
            phPidEnabled_ = true;
            LOGI("Activate pH regulation (delay=%umin)", (unsigned)runMin);
        }
        if (orpAutoMode_ && !orpPidEnabled_ && runMin >= delayPidsMin_) {
            orpPidEnabled_ = true;
            LOGI("Activate ORP regulation (delay=%umin)", (unsigned)runMin);
        }
    } else {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
    }

    bool windowActive = false;
    portENTER_CRITICAL(&pendingMux_);
    windowActive = filtrationWindowActive_;
    portEXIT_CRITICAL(&pendingMux_);

    bool filtrationDesired = filtrationFsm_.on;
    if (!autoMode_) {
        // Legacy-like manual mode: when auto_mode is off, keep filtration fully manual.
        filtrationDesired = filtrationFsm_.on;
    } else {
        if (filtrationFsm_.on && haveAirTemp && airTemp <= freezeHoldTempC_) {
            // Freeze hold: once running, never stop under freeze-hold threshold.
            filtrationDesired = true;
        } else if (psiError_) {
            filtrationDesired = false;
        } else {
            const bool scheduleDemand = windowActive;
            const bool winterDemand = winterMode_ && haveAirTemp && (airTemp < winterStartTempC_);
            filtrationDesired = (scheduleDemand || winterDemand);
        }
    }

    bool robotDesired = robotFsm_.on;
    if (autoMode_) {
        robotDesired = false;
        if (filtrationFsm_.on && !cleaningDone_) {
            const uint32_t filtrationRunMin = stateUptimeSec_(filtrationFsm_, nowMs) / 60U;
            if (filtrationRunMin >= robotDelayMin_) robotDesired = true;
        }
        if (robotFsm_.on) {
            const uint32_t robotRunMin = stateUptimeSec_(robotFsm_, nowMs) / 60U;
            if (robotRunMin >= robotDurationMin_) robotDesired = false;
        }
        if (!filtrationFsm_.on) robotDesired = false;
    }

    bool swgDesired = swgFsm_.on;
    if (autoMode_) {
        swgDesired = false;
        if (electrolyseMode_ && filtrationFsm_.on) {
            if (electroRunMode_) {
                if (swgFsm_.on) {
                    swgDesired = haveOrp && (orp <= orpSetpoint_);
                } else {
                    const bool startReady =
                        haveWaterTemp &&
                        (waterTemp >= secureElectroTempC_) &&
                        ((stateUptimeSec_(filtrationFsm_, nowMs) / 60U) >= delayElectroMin_);
                    swgDesired = startReady && haveOrp && (orp <= (orpSetpoint_ * 0.9f));
                }
            } else {
                if (swgFsm_.on) {
                    swgDesired = true;
                } else {
                    const bool startReady =
                        haveWaterTemp &&
                        (waterTemp >= secureElectroTempC_) &&
                        ((stateUptimeSec_(filtrationFsm_, nowMs) / 60U) >= delayElectroMin_);
                    swgDesired = startReady;
                }
            }
        }
    }

    bool phPumpDesired = false;
    bool orpPumpDesired = false;
    if (filtrationDesired) {
        const bool phAllowed = phPidEnabled_ && havePh && !psiError_;
        if (phAllowed) {
            uint32_t outMs = 0;
            (void)stepTemporalPid_(phPidState_,
                                   ph,
                                   phSetpoint_,
                                   phKp_,
                                   phKi_,
                                   phKd_,
                                   phWindowMs_,
                                   true,
                                   nowMs,
                                   phPumpDesired,
                                   outMs);
        } else if (phPidState_.initialized || phPidState_.outputOnMs != 0U || phPidState_.lastDemandOn) {
            resetTemporalPidState_(phPidState_, nowMs);
        }

        // ORP peristaltic dosing is disabled when electrolyse mode is active.
        const bool orpAllowed = orpPidEnabled_ && haveOrp && !electrolyseMode_ && !psiError_;
        if (orpAllowed) {
            uint32_t outMs = 0;
            (void)stepTemporalPid_(orpPidState_,
                                   orp,
                                   orpSetpoint_,
                                   orpKp_,
                                   orpKi_,
                                   orpKd_,
                                   orpWindowMs_,
                                   false,
                                   nowMs,
                                   orpPumpDesired,
                                   outMs);
        } else if (orpPidState_.initialized || orpPidState_.outputOnMs != 0U || orpPidState_.lastDemandOn) {
            resetTemporalPidState_(orpPidState_, nowMs);
        }
    } else {
        if (phPidState_.initialized || phPidState_.outputOnMs != 0U || phPidState_.lastDemandOn) {
            resetTemporalPidState_(phPidState_, nowMs);
        }
        if (orpPidState_.initialized || orpPidState_.outputOnMs != 0U || orpPidState_.lastDemandOn) {
            resetTemporalPidState_(orpPidState_, nowMs);
        }
    }

    bool fillingDesired = false;
    if (haveLevel) {
        if (!fillingFsm_.on) {
            fillingDesired = !levelOk;
        } else {
            const bool minUpReached = stateUptimeSec_(fillingFsm_, nowMs) >= fillingMinOnSec_;
            fillingDesired = !(levelOk && minUpReached);
        }
    }

    applyDeviceControl_(filtrationDeviceSlot_, "Filtration Pump", filtrationFsm_, filtrationDesired, nowMs);
    applyDeviceControl_(phPumpDeviceSlot_, "pH Pump", phPumpFsm_, phPumpDesired, nowMs);
    applyDeviceControl_(orpPumpDeviceSlot_, "Chlorine Pump", orpPumpFsm_, orpPumpDesired, nowMs);
    applyDeviceControl_(robotDeviceSlot_, "Robot Pump", robotFsm_, robotDesired, nowMs);
    applyDeviceControl_(swgDeviceSlot_, "SWG Pump", swgFsm_, swgDesired, nowMs);
    applyDeviceControl_(fillingDeviceSlot_, "Filling Pump", fillingFsm_, fillingDesired, nowMs);
}
