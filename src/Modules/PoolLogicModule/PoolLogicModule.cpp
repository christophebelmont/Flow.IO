/**
 * @file PoolLogicModule.cpp
 * @brief Implementation file.
 */

#include "PoolLogicModule.h"

#include <Arduino.h>
#include <math.h>
#include <cstring>

#define LOG_TAG "PoolLogc"
#include "Core/ModuleLog.h"

void PoolLogicModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;

    cfg.registerVar(enabledVar_);

    cfg.registerVar(autoModeVar_);
    cfg.registerVar(winterModeVar_);
    cfg.registerVar(phAutoModeVar_);
    cfg.registerVar(orpAutoModeVar_);
    cfg.registerVar(electrolyseModeVar_);
    cfg.registerVar(electroRunModeVar_);

    cfg.registerVar(tempLowVar_);
    cfg.registerVar(tempSetpointVar_);
    cfg.registerVar(startMinVar_);
    cfg.registerVar(stopMaxVar_);

    cfg.registerVar(orpIdVar_);
    cfg.registerVar(psiIdVar_);
    cfg.registerVar(waterTempIdVar_);
    cfg.registerVar(airTempIdVar_);
    cfg.registerVar(levelIdVar_);

    cfg.registerVar(psiLowVar_);
    cfg.registerVar(psiHighVar_);
    cfg.registerVar(winterStartVar_);
    cfg.registerVar(freezeHoldVar_);
    cfg.registerVar(secureElectroVar_);
    cfg.registerVar(orpSetpointVar_);

    cfg.registerVar(psiDelayVar_);
    cfg.registerVar(delayPidsVar_);
    cfg.registerVar(delayElectroVar_);
    cfg.registerVar(robotDelayVar_);
    cfg.registerVar(robotDurationVar_);
    cfg.registerVar(fillingMinOnVar_);

    cfg.registerVar(filtrationDeviceVar_);
    cfg.registerVar(swgDeviceVar_);
    cfg.registerVar(robotDeviceVar_);
    cfg.registerVar(fillingDeviceVar_);

    logHub_ = services.get<LogHubService>("loghub");
    const EventBusService* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    schedSvc_ = services.get<TimeSchedulerService>("time.scheduler");
    ioSvc_ = services.get<IOServiceV2>("io");
    poolSvc_ = services.get<PoolDeviceService>("pooldev");
    if (!ioSvc_) {
        LOGW("PoolLogic waiting for IOServiceV2");
    }
    if (!poolSvc_) {
        LOGW("PoolLogic waiting for PoolDeviceService");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolLogicModule::onEventStatic_, this);
    }

    if (!enabled_) {
        LOGI("PoolLogic disabled");
        return;
    }

    ensureDailySlot_();
    pendingDailyRecalc_ = true;

    if (schedSvc_ && schedSvc_->isActive) {
        filtrationWindowActive_ = schedSvc_->isActive(schedSvc_->ctx, SLOT_FILTR_WINDOW);
    }

    LOGI("PoolLogic ready");
    (void)cfgStore_;
    (void)logHub_;
}

void PoolLogicModule::loop()
{
    if (!enabled_) {
        vTaskDelay(pdMS_TO_TICKS(500));
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
        recalcAndApplyFiltrationWindow_();
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
    recalc.startHour = 15;
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
    if (!isfinite(waterTemp)) return false;

    int duration = 2;
    if (waterTemp < waterTempLowThreshold_) {
        duration = 2;
    } else if (waterTemp < waterTempSetpoint_) {
        duration = (int)lroundf(waterTemp / 3.0f);
    } else {
        duration = (int)lroundf(waterTemp / 2.0f);
    }

    if (duration < 2) duration = 2;
    if (duration > 24) duration = 24;

    const int startMin = (int)filtrationStartMin_;
    int stopMax = (int)filtrationStopMax_;
    if (stopMax > 23) stopMax = 23;
    if (stopMax < 0) stopMax = 0;

    int start = 15 - (int)lroundf((float)duration / 2.0f);
    if (start < startMin) start = startMin;

    int stop = start + duration;
    if (stop > stopMax) stop = stopMax;

    if (stop <= start) {
        if (start < 23) {
            stop = start + 1;
        } else {
            start = 22;
            stop = 23;
        }
    }

    durationOut = (uint8_t)(stop - start);
    startHourOut = (uint8_t)start;
    stopHourOut = (uint8_t)stop;
    return true;
}

void PoolLogicModule::recalcAndApplyFiltrationWindow_()
{
    if (!ioSvc_ || !ioSvc_->readAnalog) {
        LOGW("No IOServiceV2 available for water temperature");
        return;
    }
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("No time.scheduler service available");
        return;
    }

    float waterTemp = 0.0f;
    if (!loadAnalogSensor_(waterTempIoId_, waterTemp)) {
        LOGW("Water temperature unavailable on ioId=%u", (unsigned)waterTempIoId_);
        return;
    }

    uint8_t startHour = 0;
    uint8_t stopHour = 0;
    uint8_t duration = 0;
    if (!computeFiltrationWindow_(waterTemp, startHour, stopHour, duration)) {
        LOGW("Invalid water temperature value");
        return;
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
        return;
    }

    LOGI("Filtration duration=%uh water=%.2fC start=%uh stop=%uh",
         (unsigned)duration,
         (double)waterTemp,
         (unsigned)startHour,
         (unsigned)stopHour);
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

    if (filtrationStarted) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
    }
    if (filtrationStopped) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
    }
    if (robotStopped) {
        cleaningDone_ = true;
    }

    float psi = 0.0f;
    float waterTemp = 0.0f;
    float airTemp = 0.0f;
    float orp = 0.0f;
    bool levelOk = true;

    const bool havePsi = loadAnalogSensor_(psiIoId_, psi);
    const bool haveWaterTemp = loadAnalogSensor_(waterTempIoId_, waterTemp);
    const bool haveAirTemp = loadAnalogSensor_(airTempIoId_, airTemp);
    const bool haveOrp = loadAnalogSensor_(orpIoId_, orp);
    const bool haveLevel = loadDigitalSensor_(levelIoId_, levelOk);

    if (filtrationFsm_.on && havePsi) {
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

    bool filtrationDesired = false;
    if (filtrationFsm_.on && haveAirTemp && airTemp <= freezeHoldTempC_) {
        // Freeze hold: once running, never stop under freeze-hold threshold.
        filtrationDesired = true;
    } else if (psiError_) {
        filtrationDesired = false;
    } else {
        const bool scheduleDemand = autoMode_ && windowActive;
        const bool winterDemand = winterMode_ && haveAirTemp && (airTemp < winterStartTempC_);
        filtrationDesired = (scheduleDemand || winterDemand);
    }

    bool robotDesired = false;
    if (autoMode_ && filtrationFsm_.on && !cleaningDone_) {
        const uint32_t filtrationRunMin = stateUptimeSec_(filtrationFsm_, nowMs) / 60U;
        if (filtrationRunMin >= robotDelayMin_) robotDesired = true;
    }
    if (robotFsm_.on) {
        const uint32_t robotRunMin = stateUptimeSec_(robotFsm_, nowMs) / 60U;
        if (robotRunMin >= robotDurationMin_) robotDesired = false;
    }
    if (!filtrationFsm_.on) robotDesired = false;

    bool swgDesired = false;
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
    applyDeviceControl_(robotDeviceSlot_, "Robot Pump", robotFsm_, robotDesired, nowMs);
    applyDeviceControl_(swgDeviceSlot_, "SWG Pump", swgFsm_, swgDesired, nowMs);
    applyDeviceControl_(fillingDeviceSlot_, "Filling Pump", fillingFsm_, fillingDesired, nowMs);
}
