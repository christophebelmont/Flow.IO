/**
 * @file PoolLogicLifecycle.cpp
 * @brief Module lifecycle wiring for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Core/MqttTopics.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

#include <cstring>
#include <new>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
// PoolLogic exposes one aggregated cfg/poollogic route plus the per-branch
// routes used by HA, MQTT config sync, and tooling.
static constexpr uint8_t kPoolLogicCfgProducerId = 44;
static constexpr const char* kPoolLogicCfgTopicBase = "cfg/poollogic";
static constexpr uint8_t kCfgBranchMode = 1;
static constexpr uint8_t kCfgBranchFiltration = 2;
static constexpr uint8_t kCfgBranchSensors = 3;
static constexpr uint8_t kCfgBranchPid = 4;
static constexpr uint8_t kCfgBranchDelay = 5;
static constexpr uint8_t kCfgBranchDevice = 6;
static constexpr const char* kCfgModuleMode = "poollogic/mode";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModulePid = "poollogic/pid";
static constexpr const char* kCfgModuleDelay = "poollogic/delay";
static constexpr const char* kCfgModuleDevice = "poollogic/device";

enum : uint16_t {
    kCfgMsgBase = 1,
    kCfgMsgMode = 2,
    kCfgMsgFiltration = 3,
    kCfgMsgSensors = 4,
    kCfgMsgPid = 5,
    kCfgMsgDelay = 6,
    kCfgMsgDevice = 7,
};

static constexpr MqttConfigRouteProducer::Route kPoolLogicCfgRoutes[] = {
    {kCfgMsgBase,
     {(uint8_t)ConfigModuleId::PoolLogic, ConfigBranchRef::UnknownLocalBranch},
     nullptr,
     "",
     (uint8_t)MqttPublishPriority::Normal,
     &PoolLogicModule::buildCfgBaseStatic_,
     kPoolLogicCfgTopicBase},
    {kCfgMsgMode,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchMode},
     kCfgModuleMode,
     "mode",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgFiltration,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchFiltration},
     kCfgModuleFiltration,
     "filtration",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgSensors,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchSensors},
     kCfgModuleSensors,
     "sensors",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgPid,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchPid},
     kCfgModulePid,
     "pid",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgDelay,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchDelay},
     kCfgModuleDelay,
     "delay",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgDevice,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchDevice},
     kCfgModuleDevice,
     "device",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
};
}

void PoolLogicModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::PoolLogic;
    cfgStore_ = &cfg;
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);

    // Runtime moduleName reassignment keeps the config tree grouped by branch
    // even though the variables are declared on a single facade class.
    enabledVar_.moduleName = kCfgModuleMode;
    autoModeVar_.moduleName = kCfgModuleMode;
    winterModeVar_.moduleName = kCfgModuleMode;
    phAutoModeVar_.moduleName = kCfgModuleMode;
    orpAutoModeVar_.moduleName = kCfgModuleMode;
    phDosePlusVar_.moduleName = kCfgModuleMode;
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
    phLevelIdVar_.moduleName = kCfgModuleSensors;
    chlorineLevelIdVar_.moduleName = kCfgModuleSensors;

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
    phPumpDeviceVar_.moduleName = kCfgModuleDevice;
    orpPumpDeviceVar_.moduleName = kCfgModuleDevice;

    // Registration order mirrors the published config branches so init remains
    // easy to diff against the generated cfgdocs and MQTT routes.
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchMode);

    cfg.registerVar(autoModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(winterModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(phAutoModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(orpAutoModeVar_, kCfgModuleId, kCfgBranchMode);
    cfg.registerVar(phDosePlusVar_, kCfgModuleId, kCfgBranchMode);
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
    cfg.registerVar(phLevelIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(chlorineLevelIdVar_, kCfgModuleId, kCfgBranchSensors);

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
    cfg.registerVar(phPumpDeviceVar_, kCfgModuleId, kCfgBranchDevice);
    cfg.registerVar(orpPumpDeviceVar_, kCfgModuleId, kCfgBranchDevice);

    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    schedSvc_ = services.get<TimeSchedulerService>(ServiceId::TimeScheduler);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    poolSvc_ = services.get<PoolDeviceService>(ServiceId::PoolDevice);
    haSvc_ = services.get<HAService>(ServiceId::Ha);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    if (!ioSvc_) {
        LOGW("PoolLogic waiting for IOServiceV2");
    }
    if (!poolSvc_) {
        LOGW("PoolLogic waiting for PoolDeviceService");
    }
    // HA entities are still registered from the lifecycle layer because they
    // are part of startup wiring, not of the control algorithm itself.
    if (haSvc_ && haSvc_->addSwitch) {
        const HASwitchEntry autoModeSwitch{
            "poollogic",
            "pl_auto",
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
            "pl_winter",
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
            "pl_ph_auto",
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
            "pl_orp_auto",
            "Orp Auto-regulation",
            "cfg/poollogic/mode",
            "{% if value_json.orp_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/mode\\\":{\\\"orp_auto_mode\\\":true}}",
            "{\\\"poollogic/mode\\\":{\\\"orp_auto_mode\\\":false}}",
            "mdi:water-check-outline",
            "config"
        };
        const HASwitchEntry phDosePlusSwitch{
            "poollogic",
            "pl_ph_plus",
            "pH Dosing uses pH+",
            "cfg/poollogic/mode",
            "{% if value_json.ph_dose_plus %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/mode\\\":{\\\"ph_dose_plus\\\":true}}",
            "{\\\"poollogic/mode\\\":{\\\"ph_dose_plus\\\":false}}",
            "mdi:beaker-plus-outline",
            "config"
        };
        (void)haSvc_->addSwitch(haSvc_->ctx, &autoModeSwitch);
        (void)haSvc_->addSwitch(haSvc_->ctx, &winterModeSwitch);
        (void)haSvc_->addSwitch(haSvc_->ctx, &phAutoModeSwitch);
        (void)haSvc_->addSwitch(haSvc_->ctx, &orpAutoModeSwitch);
        (void)haSvc_->addSwitch(haSvc_->ctx, &phDosePlusSwitch);
    }
    if (haSvc_ && haSvc_->addSensor) {
        const HASensorEntry filtrationStart{
            "poollogic",
            "pl_flt_start",
            "Calculated Filtration Start",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_start_clc | int(0) }}",
            nullptr,
            "mdi:clock-start",
            "h"
        };
        const HASensorEntry filtrationStop{
            "poollogic",
            "pl_flt_stop",
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
        const HANumberEntry filtrationStartMin{
            "poollogic",
            "pl_flt_start_min",
            "Min Start Filtration Pump",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_start_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/filtration\\\":{\\\"filtr_start_min\\\":{{ value | int(0) }}}}",
            0.0f,
            23.0f,
            1.0f,
            "box",
            "config",
            "mdi:clock-start",
            "h"
        };
        const HANumberEntry filtrationStopMax{
            "poollogic",
            "pl_flt_stop_max",
            "Max End Filtration Pump",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_stop_max | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/filtration\\\":{\\\"filtr_stop_max\\\":{{ value | int(0) }}}}",
            0.0f,
            23.0f,
            1.0f,
            "box",
            "config",
            "mdi:clock-end",
            "h"
        };
        const HANumberEntry delayPidsMin{
            "poollogic",
            "pl_dly_pid",
            "Delay PIDs",
            "cfg/poollogic/delay",
            "{{ value_json.dly_pid_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/delay\\\":{\\\"dly_pid_min\\\":{{ value | int(0) }}}}",
            0.0f,
            30.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-sand",
            "min"
        };
        const HANumberEntry fillMinUptime{
            "poollogic",
            "pl_fill_min_upt",
            "Min Uptime Fill Pump",
            "cfg/poollogic/delay",
            "{{ ((value_json.fill_min_on_s | float(0)) / 60) | round(1) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/delay\\\":{\\\"fill_min_on_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
            0.0f,
            4.0f,
            0.5f,
            "box",
            "config",
            "mdi:timer-cog-outline",
            "mn"
        };
        const HANumberEntry phSetpoint{
            "poollogic",
            "pl_ph_sp",
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
            "pl_orp_sp",
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
            "pl_ph_win",
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
            "pl_orp_win",
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
            "pl_psi_low",
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
            "pl_psi_high",
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
        (void)haSvc_->addNumber(haSvc_->ctx, &filtrationStartMin);
        (void)haSvc_->addNumber(haSvc_->ctx, &filtrationStopMax);
        (void)haSvc_->addNumber(haSvc_->ctx, &delayPidsMin);
        (void)haSvc_->addNumber(haSvc_->ctx, &fillMinUptime);
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
            "pl_flt_recalc",
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
        static constexpr const char* kMqttControlCmds[] = {
            "poollogic.auto_mode.toggle",
            "poollogic.ph_auto_mode.set",
            "poollogic.ph_auto_mode.toggle",
            "poollogic.orp_auto_mode.set",
            "poollogic.orp_auto_mode.toggle",
            "poollogic.winter_mode.set",
            "poollogic.winter_mode.toggle",
            "poollogic.filtration.toggle",
            "poollogic.ph_pump.write",
            "poollogic.ph_pump.toggle",
            "poollogic.orp_pump.write",
            "poollogic.orp_pump.toggle",
            "poollogic.light.write",
            "poollogic.light.toggle",
            "poollogic.lights.write",
            "poollogic.lights.toggle",
            "poollogic.robot.write",
            "poollogic.robot.toggle",
            "poollogic.heater.write",
            "poollogic.heater.toggle",
            "poollogic.chlorine_generator.write",
            "poollogic.chlorine_generator.toggle",
            "poollogic.swg.write",
            "poollogic.swg.toggle"
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(kMqttControlCmds) / sizeof(kMqttControlCmds[0])); ++i) {
            cmdSvc_->registerHandler(cmdSvc_->ctx, kMqttControlCmds[i], &PoolLogicModule::cmdMqttControlStatic_, this);
        }
    }
    // PoolLogic owns the alarm definitions but delegates evaluation to the
    // shared alarm module through static condition callbacks.
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

        const AlarmRegistration phTankLowAlarm{
            AlarmId::PoolPhTankLow,
            AlarmSeverity::Alarm,
            false,
            500,
            1000,
            60000,
            "ph_tank_low",
            "pH tank low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &phTankLowAlarm, &PoolLogicModule::condPhTankLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPhTankLow");
        }

        const AlarmRegistration chlorineTankLowAlarm{
            AlarmId::PoolChlorineTankLow,
            AlarmSeverity::Alarm,
            false,
            500,
            1000,
            60000,
            "chlorine_tank_low",
            "Chlorine tank low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &chlorineTankLowAlarm, &PoolLogicModule::condChlorineTankLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolChlorineTankLow");
        }

        const AlarmRegistration phPumpMaxUptimeAlarm{
            AlarmId::PoolPhPumpMaxUptime,
            AlarmSeverity::Alarm,
            true,
            500,
            1000,
            60000,
            "ph_pump_max_uptime",
            "pH pump max uptime reached",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &phPumpMaxUptimeAlarm, &PoolLogicModule::condPhPumpMaxUptimeStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPhPumpMaxUptime");
        }

        const AlarmRegistration chlorinePumpMaxUptimeAlarm{
            AlarmId::PoolChlorinePumpMaxUptime,
            AlarmSeverity::Alarm,
            true,
            500,
            1000,
            60000,
            "chlorine_pump_uptime",
            "Chlorine pump max uptime reached",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &chlorinePumpMaxUptimeAlarm, &PoolLogicModule::condChlorinePumpMaxUptimeStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolChlorinePumpMaxUptime");
        }

        const AlarmRegistration waterLevelLowAlarm{
            AlarmId::PoolWaterLevelLow,
            AlarmSeverity::Alarm,
            false,
            60000,
            1000,
            60000,
            "pool_water_level_low",
            "Pool water level low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &waterLevelLowAlarm, &PoolLogicModule::condWaterLevelLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolWaterLevelLow");
        }
    } else {
        LOGW("PoolLogic running without alarm service");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolLogicModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &PoolLogicModule::onEventStatic_, this);
    }

    if (!enabled_) {
        LOGI("PoolLogic disabled");
        return;
    }

    LOGI("PoolLogic ready");
    (void)cfgStore_;
    (void)logHub_;
}

void PoolLogicModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kPoolLogicCfgProducerId,
                               kPoolLogicCfgRoutes,
                               (uint8_t)(sizeof(kPoolLogicCfgRoutes) / sizeof(kPoolLogicCfgRoutes[0])),
                               services);
    }

    if (!enabled_) return;

    LOGI("PoolLogic pH dosing mode=%s", phDosePlus_ ? "pH+" : "pH-");
    normalizeDeviceSlots_();
    logDeviceSlotConfig_();

    ensureDailySlot_();

    if (schedSvc_ && schedSvc_->isActive) {
        filtrationWindowActive_ = schedSvc_->isActive(schedSvc_->ctx, SLOT_FILTR_WINDOW);
    }

    // Trigger one recompute on startup, after persisted config and scheduler
    // blob are fully loaded, so the window reflects the final restored state.
    portENTER_CRITICAL(&pendingMux_);
    pendingDailyRecalc_ = true;
    portEXIT_CRITICAL(&pendingMux_);
}

void PoolLogicModule::loop()
{
    if (!enabled_) {
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    // The loop only consumes latched scheduler events; actual work stays in the
    // task context to avoid doing heavy operations from event callbacks.
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

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchFiltration) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrationCalcStart) == 0 ||
                strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrationCalcStop) == 0) {
                (void)applyFiltrationWindowSlot_(filtrationCalcStart_, filtrationCalcStop_);
            } else {
                portENTER_CRITICAL(&pendingMux_);
                pendingDailyRecalc_ = true;
                portEXIT_CRITICAL(&pendingMux_);
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchMode &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::AutoMode) == 0 && autoMode_) {
                portENTER_CRITICAL(&pendingMux_);
                pendingFiltrationReconcile_ = true;
                portEXIT_CRITICAL(&pendingMux_);
            } else if (strcmp(p->nvsKey, NvsKeys::PoolLogic::PhAutoMode) == 0 && phAutoMode_) {
                // Global business rule: entering pH auto starts from a safe stopped pump.
                if (!writeDeviceDesired_(phPumpDeviceSlot_, false)) {
                    LOGW("PoolLogic failed to stop pH pump on ph_auto_mode enable (slot=%u)",
                         (unsigned)phPumpDeviceSlot_);
                }
                resetTemporalPidState_(phPidState_, millis());
            } else if (strcmp(p->nvsKey, NvsKeys::PoolLogic::OrpAutoMode) == 0 && orpAutoMode_) {
                // Global business rule: entering ORP auto starts from a safe stopped pump.
                if (!writeDeviceDesired_(orpPumpDeviceSlot_, false)) {
                    LOGW("PoolLogic failed to stop ORP pump on orp_auto_mode enable (slot=%u)",
                         (unsigned)orpPumpDeviceSlot_);
                }
                resetTemporalPidState_(orpPidState_, millis());
            }
        }
        return;
    }

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

    // Scheduler edges only latch intent/state; the loop owns the control work.
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

void PoolLogicModule::normalizeDeviceSlots_()
{
    // Persisting invalid slots back to defaults keeps future boots and cfg
    // publications aligned with the effective runtime wiring.
    auto normalize = [this](uint8_t& slot,
                            uint8_t defSlot,
                            ConfigVariable<uint8_t,0>& var,
                            const char* role) {
        if (slot < POOL_DEVICE_MAX) return;
        LOGW("PoolLogic invalid device slot role=%s slot=%u -> default=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)defSlot);
        slot = defSlot;
        if (cfgStore_) {
            (void)cfgStore_->set(var, slot);
        }
    };

    normalize(filtrationDeviceSlot_, PoolBinding::kDeviceSlotFiltrationPump, filtrationDeviceVar_, "filtration");
    normalize(swgDeviceSlot_, PoolBinding::kDeviceSlotChlorineGenerator, swgDeviceVar_, "swg");
    normalize(robotDeviceSlot_, PoolBinding::kDeviceSlotRobot, robotDeviceVar_, "robot");
    normalize(fillingDeviceSlot_, PoolBinding::kDeviceSlotFillPump, fillingDeviceVar_, "filling");
    normalize(phPumpDeviceSlot_, PoolBinding::kDeviceSlotPhPump, phPumpDeviceVar_, "ph_pump");
    normalize(orpPumpDeviceSlot_, PoolBinding::kDeviceSlotChlorinePump, orpPumpDeviceVar_, "orp_pump");
}

void PoolLogicModule::logDeviceSlotConfig_() const
{
    LOGI("PoolLogic slots filtr=%u swg=%u robot=%u fill=%u ph=%u orp=%u",
         (unsigned)filtrationDeviceSlot_,
         (unsigned)swgDeviceSlot_,
         (unsigned)robotDeviceSlot_,
         (unsigned)fillingDeviceSlot_,
         (unsigned)phPumpDeviceSlot_,
         (unsigned)orpPumpDeviceSlot_);

    logDeviceSlotBinding_("filtration", filtrationDeviceSlot_, 0);
    logDeviceSlotBinding_("swg", swgDeviceSlot_, -1);
    logDeviceSlotBinding_("robot", robotDeviceSlot_, -1);
    logDeviceSlotBinding_("filling", fillingDeviceSlot_, -1);
    logDeviceSlotBinding_("ph_pump", phPumpDeviceSlot_, 1);
    logDeviceSlotBinding_("orp_pump", orpPumpDeviceSlot_, 1);
}

void PoolLogicModule::logDeviceSlotBinding_(const char* role, uint8_t slot, int8_t expectedType) const
{
    // Cross-check the static PoolBinding map with live PoolDevice metadata to
    // surface miswired or inconsistent configurations early in the logs.
    const PoolIoBinding* binding = PoolBinding::ioBindingBySlot(slot);
    if (binding) {
        LOGI("PoolLogic role=%s slot=%u io=%u map=%s",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)binding->ioId,
             binding->name ? binding->name : "?");
    } else {
        LOGW("PoolLogic role=%s slot=%u has no static IO map", role ? role : "?", (unsigned)slot);
    }

    if (!poolSvc_ || !poolSvc_->meta) {
        LOGW("PoolLogic role=%s slot=%u PDM meta unavailable", role ? role : "?", (unsigned)slot);
        return;
    }

    PoolDeviceSvcMeta meta{};
    const PoolDeviceSvcStatus st = poolSvc_->meta(poolSvc_->ctx, slot, &meta);
    if (st != POOLDEV_SVC_OK) {
        LOGW("PoolLogic role=%s slot=%u PDM meta failed st=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)st);
        return;
    }

    LOGI("PoolLogic role=%s slot=%u pdm used=%u type=%u io=%u label=%s",
         role ? role : "?",
         (unsigned)slot,
         (unsigned)meta.used,
         (unsigned)meta.type,
         (unsigned)meta.ioId,
         meta.label[0] ? meta.label : "?");

    if (binding && meta.ioId != binding->ioId) {
        LOGW("PoolLogic role=%s slot=%u io mismatch map=%u pdm=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)binding->ioId,
             (unsigned)meta.ioId);
    }

    if (expectedType >= 0 && meta.type != (uint8_t)expectedType) {
        LOGW("PoolLogic role=%s slot=%u type mismatch expected=%u got=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)expectedType,
             (unsigned)meta.type);
    }
}
