/**
 * @file TimeModule.cpp
 * @brief Implementation file.
 */
#include "TimeModule.h"
#include "Core/Runtime.h"
#include "Core/CommandRegistry.h"
#include <time.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>
#define LOG_TAG "TimeModl"
#include "Core/ModuleLog.h"

// Fast-clock test mode:
// Uncomment the line below to simulate time from 2020-01-01 00:00:00.
// In this mode, 5 minutes of real time == 1 simulated month.
// #define TIME_TEST_FAST_CLOCK

static uint32_t clampU32(uint32_t v, uint32_t minV, uint32_t maxV) {
    if (v < minV) return minV;
    if (v > maxV) return maxV;
    return v;
}

static const char* schedulerEdgeStr(uint8_t edge)
{
    if (edge == (uint8_t)SchedulerEdge::Start) return "start";
    if (edge == (uint8_t)SchedulerEdge::Stop) return "stop";
    return "trigger";
}

static const char* findJsonStringValueLocal(const char* json, const char* key)
{
    static char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key ? key : "");
    const char* p = strstr(json ? json : "", pat);
    if (!p) return nullptr;
    return p + strlen(pat);
}

static const char* findJsonValueLocal(const char* json, const char* key)
{
    static char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key ? key : "");
    const char* p = strstr(json ? json : "", pat);
    if (!p) return nullptr;
    return p + strlen(pat);
}

static const char* skipWsLocal(const char* p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

static bool parseBoolValueLocal(const char* json, const char* key, bool& out, bool required)
{
    const char* v = skipWsLocal(findJsonValueLocal(json, key));
    if (!v) return !required;
    if (strncmp(v, "true", 4) == 0) {
        out = true;
        return true;
    }
    if (strncmp(v, "false", 5) == 0) {
        out = false;
        return true;
    }
    char* end = nullptr;
    long num = strtol(v, &end, 10);
    if (end == v) return false;
    out = (num != 0);
    return true;
}

static bool parseU32ValueLocal(const char* json, const char* key, uint32_t& out, bool required)
{
    const char* v = skipWsLocal(findJsonValueLocal(json, key));
    if (!v) return !required;
    char* end = nullptr;
    unsigned long num = strtoul(v, &end, 10);
    if (end == v) return false;
    out = (uint32_t)num;
    return true;
}

static bool parseU64ValueLocal(const char* json, const char* key, uint64_t& out, bool required)
{
    const char* v = skipWsLocal(findJsonValueLocal(json, key));
    if (!v) return !required;
    char* end = nullptr;
    unsigned long long num = strtoull(v, &end, 10);
    if (end == v) return false;
    out = (uint64_t)num;
    return true;
}

static bool parseStringValueLocal(const char* json, const char* key, char* out, size_t outLen, bool required)
{
    if (!out || outLen == 0) return false;
    const char* start = skipWsLocal(findJsonStringValueLocal(json, key));
    if (!start) return !required;
    const char* end = strchr(start, '"');
    if (!end) return false;
    size_t n = (size_t)(end - start);
    if (n >= outLen) n = outLen - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    return true;
}

#ifdef TIME_TEST_FAST_CLOCK
static constexpr uint32_t FASTCLK_REAL_MS_PER_MONTH = 5UL * 60UL * 1000UL;

static bool isLeapYearFastClock(int year)
{
    if ((year % 400) == 0) return true;
    if ((year % 100) == 0) return false;
    return (year % 4) == 0;
}

static uint8_t daysInMonthFastClock(int year, int month1to12)
{
    static const uint8_t DAYS[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month1to12 < 1 || month1to12 > 12) return 30;
    if (month1to12 == 2 && isLeapYearFastClock(year)) return 29;
    return DAYS[month1to12 - 1];
}
#endif

void TimeModule::setState(TimeSyncState s) {
    const TimeSyncState prev = state;
    state = s;
    stateTs = millis();

    if (dataStore) {
        setTimeReady(*dataStore, s == TimeSyncState::Synced);
    }

    if (prev != TimeSyncState::Synced && s == TimeSyncState::Synced) {
        // Re-evaluate and replay active slots when time becomes valid.
        schedInitialized_ = false;
    } else if (prev == TimeSyncState::Synced && s != TimeSyncState::Synced) {
        // Invalidate runtime active states until next sync.
        portENTER_CRITICAL(&schedMux_);
        for (uint8_t i = 0; i < TIME_SCHED_MAX_SLOTS; ++i) {
            sched_[i].active = false;
            sched_[i].lastTriggerMinuteKey = INVALID_MINUTE_KEY;
        }
        activeMaskValue_ = 0;
        schedInitialized_ = false;
        portEXIT_CRITICAL(&schedMux_);
    }
}

TimeSyncState TimeModule::svcState(void* ctx) {
    return static_cast<TimeModule*>(ctx)->state;
}

bool TimeModule::svcIsSynced(void* ctx) {
    auto self = static_cast<TimeModule*>(ctx);
    return self->state == TimeSyncState::Synced;
}

uint64_t TimeModule::svcEpoch(void* ctx) {
    TimeModule* self = static_cast<TimeModule*>(ctx);
    if (!self) {
        time_t now;
        time(&now);
        return (uint64_t)now;
    }
    return (uint64_t)self->nowEpoch_();
}

bool TimeModule::svcFormatLocalTime(void* ctx, char* out, size_t len) {
    TimeModule* self = static_cast<TimeModule*>(ctx);
    struct tm t;
    if (self) {
        time_t now = self->nowEpoch_();
        if (!localtime_r(&now, &t)) return false;
    } else {
        if (!getLocalTime(&t, 50)) return false;
    }
    snprintf(out, len, "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}

bool TimeModule::svcSchedSetSlot(void* ctx, const TimeSchedulerSlot* slotDef)
{
    if (!ctx || !slotDef) return false;
    return static_cast<TimeModule*>(ctx)->setSlot_(*slotDef);
}

bool TimeModule::svcSchedGetSlot(void* ctx, uint8_t slot, TimeSchedulerSlot* outDef)
{
    if (!ctx || !outDef) return false;
    return static_cast<TimeModule*>(ctx)->getSlot_(slot, *outDef);
}

bool TimeModule::svcSchedClearSlot(void* ctx, uint8_t slot)
{
    if (!ctx) return false;
    return static_cast<TimeModule*>(ctx)->clearSlot_(slot);
}

bool TimeModule::svcSchedClearAll(void* ctx)
{
    if (!ctx) return false;
    return static_cast<TimeModule*>(ctx)->clearAllSlots_();
}

uint8_t TimeModule::svcSchedUsedCount(void* ctx)
{
    if (!ctx) return 0;
    return static_cast<TimeModule*>(ctx)->usedCount_();
}

uint16_t TimeModule::svcSchedActiveMask(void* ctx)
{
    if (!ctx) return 0;
    return static_cast<TimeModule*>(ctx)->activeMask_();
}

bool TimeModule::svcSchedIsActive(void* ctx, uint8_t slot)
{
    if (!ctx) return false;
    return static_cast<TimeModule*>(ctx)->isActive_(slot);
}

bool TimeModule::isSystemSlot_(uint8_t slot) const
{
    return slot < TIME_SLOT_SYS_RESERVED_COUNT;
}

void TimeModule::sanitizeLabel_(char* label)
{
    if (!label) return;
    for (size_t i = 0; i < TIME_SCHED_LABEL_MAX; ++i) {
        if (label[i] == '\0') break;
        const unsigned char c = (unsigned char)label[i];
        const bool ok =
            ((c >= 'a' && c <= 'z') ||
             (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') ||
             c == '_' || c == '-' || c == '.');
        if (!ok || !isprint(c)) {
            label[i] = '_';
        }
    }
    label[TIME_SCHED_LABEL_MAX - 1] = '\0';
}

bool TimeModule::isMonthStartEvent_(const SchedulerSlotRuntime& slotRt, const tm& localNow) const
{
    if (slotRt.def.mode != TimeSchedulerMode::RecurringClock) return false;
    if (slotRt.def.hasEnd) return false;
    if (slotRt.def.slot != TIME_SLOT_SYS_MONTH_START) return false;
    if (slotRt.def.eventId != TIME_EVENT_SYS_MONTH_START) return false;
    return localNow.tm_mday == 1;
}

void TimeModule::applySystemSlots_(SchedulerSlotRuntime* slots, size_t count) const
{
    if (!slots || count < TIME_SLOT_SYS_RESERVED_COUNT) return;

    auto setRecurringEvent = [&](uint8_t slot, uint16_t eventId, uint8_t weekdayMask, const char* label) {
        SchedulerSlotRuntime& s = slots[slot];
        s.used = true;
        s.active = false;
        s.lastTriggerMinuteKey = INVALID_MINUTE_KEY;
        s.def.slot = slot;
        s.def.eventId = eventId;
        s.def.enabled = true;
        s.def.hasEnd = false;
        s.def.replayStartOnBoot = false;
        s.def.mode = TimeSchedulerMode::RecurringClock;
        s.def.weekdayMask = weekdayMask;
        s.def.startHour = 0;
        s.def.startMinute = 0;
        s.def.endHour = 0;
        s.def.endMinute = 0;
        s.def.startEpochSec = 0;
        s.def.endEpochSec = 0;
        s.def.label[0] = '\0';
        if (label && label[0] != '\0') {
            strncpy(s.def.label, label, sizeof(s.def.label) - 1);
            s.def.label[sizeof(s.def.label) - 1] = '\0';
            sanitizeLabel_(s.def.label);
        }
    };

    setRecurringEvent(TIME_SLOT_SYS_DAY_START, TIME_EVENT_SYS_DAY_START, TIME_WEEKDAY_ALL, "sys_day_start");
    setRecurringEvent(
        TIME_SLOT_SYS_WEEK_START,
        TIME_EVENT_SYS_WEEK_START,
        cfgData.weekStartMonday ? TIME_WEEKDAY_MON : TIME_WEEKDAY_SUN,
        "sys_week_start");
    setRecurringEvent(TIME_SLOT_SYS_MONTH_START, TIME_EVENT_SYS_MONTH_START, TIME_WEEKDAY_ALL, "sys_month_start");
}

void TimeModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    cfgStore = &cfg;

    cfg.registerVar(server1Var);
    cfg.registerVar(server2Var);
    cfg.registerVar(tzVar);
    cfg.registerVar(enabledVar);
    cfg.registerVar(weekStartMondayVar);
    cfg.registerVar(scheduleBlobVar);

    logHub = services.get<LogHubService>("loghub");

    auto* ebSvc = services.get<EventBusService>("eventbus");
    eventBus = ebSvc ? ebSvc->bus : nullptr;

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore = dsSvc ? dsSvc->store : nullptr;

    if (eventBus) {
        eventBus->subscribe(EventId::DataChanged, &TimeModule::onEventStatic, this);
        eventBus->subscribe(EventId::ConfigChanged, &TimeModule::onEventStatic, this);
    }

    cmdSvc = services.get<CommandService>("cmd");
    if (cmdSvc) {
        cmdSvc->registerHandler(cmdSvc->ctx, "time.resync", cmdResync, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "ntp.resync", cmdResync, this); // backward compatibility
        cmdSvc->registerHandler(cmdSvc->ctx, "time.scheduler.info", cmdSchedInfo, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "time.scheduler.get", cmdSchedGet, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "time.scheduler.set", cmdSchedSet, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "time.scheduler.clear", cmdSchedClear, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "time.scheduler.clear_all", cmdSchedClearAll, this);
    }

    static TimeService timeSvc{
        svcState,
        svcIsSynced,
        svcEpoch,
        svcFormatLocalTime,
        nullptr
    };
    timeSvc.ctx = this;
    services.add("time", &timeSvc);

    static TimeSchedulerService schedSvc{
        svcSchedSetSlot,
        svcSchedGetSlot,
        svcSchedClearSlot,
        svcSchedClearAll,
        svcSchedUsedCount,
        svcSchedActiveMask,
        svcSchedIsActive,
        nullptr
    };
    schedSvc.ctx = this;
    services.add("time.scheduler", &schedSvc);

    LOGI("Time services registered (time, time.scheduler)");

    _netReady = false;
    _netReadyTs = 0;
    _retryCount = 0;
    _retryDelayMs = 2000;
#ifdef TIME_TEST_FAST_CLOCK
    simBootMs_ = millis();
    setenv("TZ", cfgData.tz, 1);
    tzset();
    LOGW("FAST CLOCK test mode enabled: start=2020-01-01, 1 month=5 minutes");
#endif

    resetScheduleRuntime_();
    schedNeedsReload_ = true;

    setState(cfgData.enabled ? TimeSyncState::WaitingNetwork : TimeSyncState::Disabled);
}

void TimeModule::loop() {
    if (schedNeedsReload_) {
        (void)loadScheduleFromBlob_();
    }

#ifdef TIME_TEST_FAST_CLOCK
    if (cfgData.enabled) {
        if (state != TimeSyncState::Synced) {
            setState(TimeSyncState::Synced);
        }
        tickScheduler_();
        vTaskDelay(pdMS_TO_TICKS(250));
        return;
    }
#endif

    if (!cfgData.enabled) {
        if (state != TimeSyncState::Disabled) setState(TimeSyncState::Disabled);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return;
    }

    switch (state) {

    case TimeSyncState::WaitingNetwork:
        // If network becomes ready, onEvent() updates _netReady and _netReadyTs.
        if (_netReady) {
            constexpr uint32_t WARMUP_MS = 2000;
            if (millis() - _netReadyTs >= WARMUP_MS) {
                LOGI("Network warmup done -> start syncing");
                setState(TimeSyncState::Syncing);
            }
        }
        break;

    case TimeSyncState::Syncing: {
        LOGI("Syncing via NTP...");

        configTzTime(cfgData.tz, cfgData.server1, cfgData.server2);

        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 4000)) {
            char buf[32];
            svcFormatLocalTime(nullptr, buf, sizeof(buf));
            LOGI("Synced ok: %s", buf);

            _retryCount = 0;
            _retryDelayMs = 2000;

            setState(TimeSyncState::Synced);
        } else {
            LOGW("Sync failed -> retry in %lu ms", (unsigned long)_retryDelayMs);
            setState(TimeSyncState::ErrorWait);
        }
        break;
    }

    case TimeSyncState::ErrorWait:
        if (!_netReady) {
            setState(TimeSyncState::WaitingNetwork);
            break;
        }

        if (millis() - stateTs >= _retryDelayMs) {
            _retryCount++;
            uint32_t next = _retryDelayMs;

            if      (next < 5000)   next = 5000;
            else if (next < 10000)  next = 10000;
            else if (next < 30000)  next = 30000;
            else if (next < 60000)  next = 60000;
            else                    next = 300000;

            _retryDelayMs = clampU32(next, 2000, 300000);
            setState(TimeSyncState::Syncing);
        }
        break;

    case TimeSyncState::Synced:
        if (_netReady && (millis() - stateTs > 6UL * 3600UL * 1000UL)) {
            setState(TimeSyncState::Syncing);
        }
        break;

    case TimeSyncState::Disabled:
        setState(TimeSyncState::WaitingNetwork);
        break;
    }

    tickScheduler_();
    vTaskDelay(pdMS_TO_TICKS(250));
}

void TimeModule::forceResync() {
    if (!cfgData.enabled) return;
    if (!_netReady) {
        setState(TimeSyncState::WaitingNetwork);
        return;
    }

    _retryCount = 0;
    _retryDelayMs = 2000;
    _netReadyTs = millis();
    setState(TimeSyncState::WaitingNetwork);
}

bool TimeModule::cmdResync(void* userCtx,
                           const CommandRequest&,
                           char* reply,
                           size_t replyLen)
{
    TimeModule* self = (TimeModule*)userCtx;
    self->forceResync();
    snprintf(reply, replyLen, "{\"ok\":true}");
    return true;
}

bool TimeModule::cmdSchedInfo(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    TimeModule* self = (TimeModule*)userCtx;
    if (!self) return false;
    return self->handleCmdSchedInfo_(req, reply, replyLen);
}

bool TimeModule::cmdSchedGet(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    TimeModule* self = (TimeModule*)userCtx;
    if (!self) return false;
    return self->handleCmdSchedGet_(req, reply, replyLen);
}

bool TimeModule::cmdSchedSet(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    TimeModule* self = (TimeModule*)userCtx;
    if (!self) return false;
    return self->handleCmdSchedSet_(req, reply, replyLen);
}

bool TimeModule::cmdSchedClear(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    TimeModule* self = (TimeModule*)userCtx;
    if (!self) return false;
    return self->handleCmdSchedClear_(req, reply, replyLen);
}

bool TimeModule::cmdSchedClearAll(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    TimeModule* self = (TimeModule*)userCtx;
    if (!self) return false;
    return self->handleCmdSchedClearAll_(req, reply, replyLen);
}

bool TimeModule::handleCmdSchedInfo_(const CommandRequest&, char* reply, size_t replyLen)
{
    char now[32] = {0};
    if (!svcFormatLocalTime(this, now, sizeof(now))) {
        strncpy(now, "n/a", sizeof(now) - 1);
        now[sizeof(now) - 1] = '\0';
    }
    const uint16_t mask = activeMask_();
    const uint8_t used = usedCount_();
    snprintf(reply, replyLen,
             "{\"ok\":true,\"state\":%u,\"synced\":%s,\"used\":%u,\"active_mask\":%u,"
             "\"active_mask_hex\":\"0x%04X\",\"week_start\":\"%s\",\"now\":\"%s\"}",
             (unsigned)state,
             (state == TimeSyncState::Synced) ? "true" : "false",
             (unsigned)used,
             (unsigned)mask,
             (unsigned)mask,
             cfgData.weekStartMonday ? "monday" : "sunday",
             now);
    return true;
}

bool TimeModule::handleCmdSchedGet_(const CommandRequest& req, char* reply, size_t replyLen)
{
    const char* json = req.args ? req.args : req.json;
    if (!json) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_args\"}");
        return false;
    }

    uint32_t slot = 0;
    if (!parseU32ValueLocal(json, "slot", slot, true) || slot >= TIME_SCHED_MAX_SLOTS) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_slot\"}");
        return false;
    }

    TimeSchedulerSlot def{};
    if (!getSlot_((uint8_t)slot, def)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"unused_slot\"}");
        return false;
    }

    const char* mode = (def.mode == TimeSchedulerMode::OneShotEpoch) ? "one_shot_epoch" : "recurring_clock";
    snprintf(reply, replyLen,
             "{\"ok\":true,\"slot\":%u,\"event_id\":%u,\"label\":\"%s\",\"enabled\":%s,"
             "\"mode\":\"%s\",\"has_end\":%s,\"replay_start_on_boot\":%s,"
             "\"weekday_mask\":%u,\"start\":{\"hour\":%u,\"minute\":%u,\"epoch\":%llu},"
             "\"end\":{\"hour\":%u,\"minute\":%u,\"epoch\":%llu}}",
             (unsigned)def.slot,
             (unsigned)def.eventId,
             def.label,
             def.enabled ? "true" : "false",
             mode,
             def.hasEnd ? "true" : "false",
             def.replayStartOnBoot ? "true" : "false",
             (unsigned)def.weekdayMask,
             (unsigned)def.startHour,
             (unsigned)def.startMinute,
             (unsigned long long)def.startEpochSec,
             (unsigned)def.endHour,
             (unsigned)def.endMinute,
             (unsigned long long)def.endEpochSec);
    return true;
}

bool TimeModule::handleCmdSchedSet_(const CommandRequest& req, char* reply, size_t replyLen)
{
    const char* json = req.args ? req.args : req.json;
    if (!json) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_args\"}");
        return false;
    }

    uint32_t slot = 0;
    if (!parseU32ValueLocal(json, "slot", slot, true) || slot >= TIME_SCHED_MAX_SLOTS) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_slot\"}");
        return false;
    }
    if (isSystemSlot_((uint8_t)slot)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"reserved_slot\"}");
        return false;
    }

    TimeSchedulerSlot def{};
    if (!getSlot_((uint8_t)slot, def)) {
        def = TimeSchedulerSlot{};
        def.slot = (uint8_t)slot;
        def.enabled = true;
        def.hasEnd = false;
        def.replayStartOnBoot = true;
        def.mode = TimeSchedulerMode::RecurringClock;
        def.weekdayMask = TIME_WEEKDAY_ALL;
    }
    def.slot = (uint8_t)slot;

    uint32_t eventId = 0;
    const bool hasEventId = (findJsonValueLocal(json, "event_id") != nullptr);
    if (hasEventId) {
        if (!parseU32ValueLocal(json, "event_id", eventId, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_event_id\"}");
            return false;
        }
        def.eventId = (uint16_t)eventId;
    } else if (def.eventId == 0) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_event_id\"}");
        return false;
    }

    char modeBuf[20] = {0};
    if (findJsonStringValueLocal(json, "mode")) {
        if (!parseStringValueLocal(json, "mode", modeBuf, sizeof(modeBuf), true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_mode\"}");
            return false;
        }
        if (strcmp(modeBuf, "one_shot_epoch") == 0 ||
            strcmp(modeBuf, "oneshot_epoch") == 0 ||
            strcmp(modeBuf, "oneshot") == 0 ||
            strcmp(modeBuf, "epoch") == 0) {
            def.mode = TimeSchedulerMode::OneShotEpoch;
        } else if (strcmp(modeBuf, "recurring_clock") == 0 ||
                   strcmp(modeBuf, "recurring") == 0 ||
                   strcmp(modeBuf, "clock") == 0) {
            def.mode = TimeSchedulerMode::RecurringClock;
        } else {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_mode\"}");
            return false;
        }
    } else if (findJsonValueLocal(json, "mode")) {
        uint32_t modeNum = 0;
        if (!parseU32ValueLocal(json, "mode", modeNum, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_mode\"}");
            return false;
        }
        def.mode = (modeNum == 0) ? TimeSchedulerMode::RecurringClock : TimeSchedulerMode::OneShotEpoch;
    }

    if (!parseBoolValueLocal(json, "enabled", def.enabled, false) ||
        !parseBoolValueLocal(json, "has_end", def.hasEnd, false) ||
        !parseBoolValueLocal(json, "replay_start_on_boot", def.replayStartOnBoot, false)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_bool\"}");
        return false;
    }

    uint32_t value = 0;
    if (findJsonValueLocal(json, "weekday_mask")) {
        if (!parseU32ValueLocal(json, "weekday_mask", value, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_weekday_mask\"}");
            return false;
        }
        def.weekdayMask = (uint8_t)value;
    }
    if (findJsonValueLocal(json, "start_hour")) {
        if (!parseU32ValueLocal(json, "start_hour", value, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_start_hour\"}");
            return false;
        }
        def.startHour = (uint8_t)value;
    }
    if (findJsonValueLocal(json, "start_minute")) {
        if (!parseU32ValueLocal(json, "start_minute", value, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_start_minute\"}");
            return false;
        }
        def.startMinute = (uint8_t)value;
    }
    if (findJsonValueLocal(json, "end_hour")) {
        if (!parseU32ValueLocal(json, "end_hour", value, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_end_hour\"}");
            return false;
        }
        def.endHour = (uint8_t)value;
    }
    if (findJsonValueLocal(json, "end_minute")) {
        if (!parseU32ValueLocal(json, "end_minute", value, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_end_minute\"}");
            return false;
        }
        def.endMinute = (uint8_t)value;
    }

    uint64_t value64 = 0;
    if (findJsonValueLocal(json, "start_epoch_sec")) {
        if (!parseU64ValueLocal(json, "start_epoch_sec", value64, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_start_epoch\"}");
            return false;
        }
        def.startEpochSec = value64;
    }
    if (findJsonValueLocal(json, "end_epoch_sec")) {
        if (!parseU64ValueLocal(json, "end_epoch_sec", value64, true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_end_epoch\"}");
            return false;
        }
        def.endEpochSec = value64;
    }

    char label[TIME_SCHED_LABEL_MAX] = {0};
    if (findJsonStringValueLocal(json, "label")) {
        if (!parseStringValueLocal(json, "label", label, sizeof(label), true)) {
            snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_label\"}");
            return false;
        }
        strncpy(def.label, label, sizeof(def.label) - 1);
        def.label[sizeof(def.label) - 1] = '\0';
    }

    if (!setSlot_(def)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"set_failed\"}");
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u,\"event_id\":%u}", (unsigned)def.slot, (unsigned)def.eventId);
    return true;
}

bool TimeModule::handleCmdSchedClear_(const CommandRequest& req, char* reply, size_t replyLen)
{
    const char* json = req.args ? req.args : req.json;
    if (!json) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_args\"}");
        return false;
    }

    uint32_t slot = 0;
    if (!parseU32ValueLocal(json, "slot", slot, true) || slot >= TIME_SCHED_MAX_SLOTS) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"invalid_slot\"}");
        return false;
    }
    if (isSystemSlot_((uint8_t)slot)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"reserved_slot\"}");
        return false;
    }
    if (!clearSlot_((uint8_t)slot)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"clear_failed\"}");
        return false;
    }
    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u}", (unsigned)slot);
    return true;
}

bool TimeModule::handleCmdSchedClearAll_(const CommandRequest&, char* reply, size_t replyLen)
{
    if (!clearAllSlots_()) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"clear_all_failed\"}");
        return false;
    }
    snprintf(reply, replyLen, "{\"ok\":true}");
    return true;
}

void TimeModule::onEventStatic(const Event& e, void* user)
{
    static_cast<TimeModule*>(user)->onEvent(e);
}

void TimeModule::onEvent(const Event& e)
{
    if (e.id == EventId::DataChanged) {
        if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (p->id != DATAKEY_WIFI_READY) return;
        if (!dataStore) return;

        bool ready = wifiReady(*dataStore);
        if (ready == _netReady) return;

        _netReady = ready;
        _netReadyTs = millis();

        if (_netReady) {
            LOGI("DataStore networkReady=true -> warmup");
            if (state == TimeSyncState::Synced) return;
            setState(TimeSyncState::WaitingNetwork);
        } else {
            LOGI("DataStore networkReady=false -> stop and wait");
            setState(TimeSyncState::WaitingNetwork);
        }
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (p->nvsKey[0] == '\0') return;

        if (strcmp(p->nvsKey, scheduleBlobVar.nvsKey) == 0 ||
            strcmp(p->nvsKey, weekStartMondayVar.nvsKey) == 0) {
            schedNeedsReload_ = true;
        }
        return;
    }

}

void TimeModule::resetScheduleRuntime_()
{
    portENTER_CRITICAL(&schedMux_);
    for (uint8_t i = 0; i < TIME_SCHED_MAX_SLOTS; ++i) {
        sched_[i] = SchedulerSlotRuntime{};
        sched_[i].def.slot = i;
    }
    activeMaskValue_ = 0;
    schedInitialized_ = false;
    portEXIT_CRITICAL(&schedMux_);
}

time_t TimeModule::nowEpoch_() const
{
#ifdef TIME_TEST_FAST_CLOCK
    const uint64_t elapsedMs = (uint32_t)(millis() - simBootMs_);
    const uint64_t monthIndex = elapsedMs / FASTCLK_REAL_MS_PER_MONTH;
    const uint64_t remMs = elapsedMs % FASTCLK_REAL_MS_PER_MONTH;

    const int year = 2020 + (int)(monthIndex / 12ULL);
    const int month1 = 1 + (int)(monthIndex % 12ULL);
    const uint32_t secInMonth = (uint32_t)daysInMonthFastClock(year, month1) * 86400UL;
    const uint64_t secIntoMonth = (remMs * (uint64_t)secInMonth) / (uint64_t)FASTCLK_REAL_MS_PER_MONTH;

    struct tm start{};
    start.tm_year = year - 1900;
    start.tm_mon = month1 - 1;
    start.tm_mday = 1;
    start.tm_hour = 0;
    start.tm_min = 0;
    start.tm_sec = 0;
    time_t startEpoch = mktime(&start);
    if (startEpoch <= 0) return (time_t)1577836800; // 2020-01-01T00:00:00Z fallback
    return startEpoch + (time_t)secIntoMonth;
#else
    time_t now;
    time(&now);
    return now;
#endif
}

uint8_t TimeModule::weekBitFromTm_(const tm& localNow)
{
    return (localNow.tm_wday == 0) ? 6u : (uint8_t)(localNow.tm_wday - 1);
}

uint32_t TimeModule::minuteOfDay_(const tm& localNow)
{
    return (uint32_t)localNow.tm_hour * 60u + (uint32_t)localNow.tm_min;
}

bool TimeModule::isWeekdayEnabled_(uint8_t mask, uint8_t weekBit)
{
    if (mask == 0) mask = TIME_WEEKDAY_ALL;
    return (mask & (uint8_t)(1u << weekBit)) != 0;
}

bool TimeModule::isRecurringTriggerNow_(const TimeSchedulerSlot& def, uint8_t weekBit, uint32_t minuteOfDay)
{
    if (def.mode != TimeSchedulerMode::RecurringClock) return false;
    if (!isWeekdayEnabled_(def.weekdayMask, weekBit)) return false;
    const uint32_t startMin = (uint32_t)def.startHour * 60u + (uint32_t)def.startMinute;
    return minuteOfDay == startMin;
}

bool TimeModule::isRecurringActiveNow_(const TimeSchedulerSlot& def, uint8_t weekBit, uint8_t prevWeekBit,
                                       uint32_t minuteOfDay)
{
    if (def.mode != TimeSchedulerMode::RecurringClock) return false;
    if (!def.hasEnd) return false;

    const uint32_t startMin = (uint32_t)def.startHour * 60u + (uint32_t)def.startMinute;
    const uint32_t endMin = (uint32_t)def.endHour * 60u + (uint32_t)def.endMinute;

    if (startMin == endMin) return false;

    if (startMin < endMin) {
        if (!isWeekdayEnabled_(def.weekdayMask, weekBit)) return false;
        return (minuteOfDay >= startMin && minuteOfDay < endMin);
    }

    if (minuteOfDay >= startMin) {
        return isWeekdayEnabled_(def.weekdayMask, weekBit);
    }

    return isWeekdayEnabled_(def.weekdayMask, prevWeekBit);
}

bool TimeModule::loadScheduleFromBlob_()
{
    // Build directly in live scheduler table to avoid large stack allocations
    // in the time task (ESP32 stack is tight and this path runs at boot).
    portENTER_CRITICAL(&schedMux_);
    for (uint8_t i = 0; i < TIME_SCHED_MAX_SLOTS; ++i) {
        sched_[i] = SchedulerSlotRuntime{};
        sched_[i].def.slot = i;
    }

    const char* p = scheduleBlob_;
    while (p && *p) {
        while (*p == ';' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
        if (*p == '\0') break;

        char token[160] = {0};
        size_t t = 0;
        while (*p && *p != ';') {
            if (t + 1 < sizeof(token)) token[t++] = *p;
            ++p;
        }
        if (*p == ';') ++p;
        token[t] = '\0';

        unsigned slot = 0, eventId = 0, flags = 0, weekdayMask = 0;
        unsigned startH = 0, startM = 0, endH = 0, endM = 0;
        unsigned long long startEpoch = 0, endEpoch = 0;
        char label[TIME_SCHED_LABEL_MAX] = {0};

        int n = sscanf(token, "%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%23[^;]",
                       &slot, &eventId, &flags, &weekdayMask,
                       &startH, &startM, &endH, &endM,
                       &startEpoch, &endEpoch, label);
        if (n < 10) continue;
        if (slot >= TIME_SCHED_MAX_SLOTS) continue;

        TimeSchedulerSlot def{};
        def.slot = (uint8_t)slot;
        def.eventId = (uint16_t)eventId;
        def.enabled = (flags & 0x01u) != 0;
        def.hasEnd = (flags & 0x04u) != 0;
        def.mode = ((flags & 0x08u) != 0) ? TimeSchedulerMode::OneShotEpoch : TimeSchedulerMode::RecurringClock;
        def.replayStartOnBoot = (flags & 0x10u) != 0;

        def.weekdayMask = (uint8_t)(weekdayMask & TIME_WEEKDAY_ALL);
        if (def.weekdayMask == 0) def.weekdayMask = TIME_WEEKDAY_ALL;

        def.startHour = (uint8_t)startH;
        def.startMinute = (uint8_t)startM;
        def.endHour = (uint8_t)endH;
        def.endMinute = (uint8_t)endM;
        def.startEpochSec = (uint64_t)startEpoch;
        def.endEpochSec = (uint64_t)endEpoch;
        def.label[0] = '\0';
        if (n >= 11 && label[0] != '\0') {
            strncpy(def.label, label, sizeof(def.label) - 1);
            def.label[sizeof(def.label) - 1] = '\0';
            sanitizeLabel_(def.label);
        }

        bool valid = true;
        if (def.mode == TimeSchedulerMode::RecurringClock) {
            if (def.startHour > 23 || def.startMinute > 59) valid = false;
            if (def.hasEnd && (def.endHour > 23 || def.endMinute > 59)) valid = false;
        } else {
            if (def.startEpochSec < 1609459200ULL) valid = false;
            if (def.hasEnd && def.endEpochSec <= def.startEpochSec) valid = false;
        }

        if (!valid) continue;

        SchedulerSlotRuntime& s = sched_[def.slot];
        s.used = true;
        s.def = def;
        s.active = false;
        s.lastTriggerMinuteKey = INVALID_MINUTE_KEY;
    }

    // Ensure the first 3 slots are always reserved for system cadence events.
    applySystemSlots_(sched_, TIME_SCHED_MAX_SLOTS);
    activeMaskValue_ = 0;
    schedInitialized_ = false;
    schedNeedsReload_ = false;
    portEXIT_CRITICAL(&schedMux_);

    LOGI("Scheduler loaded from NVS blob");
    return true;
}

bool TimeModule::serializeSchedule_(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    size_t pos = 0;
    for (uint8_t i = 0; i < TIME_SCHED_MAX_SLOTS; ++i) {
        SchedulerSlotRuntime s{};
        portENTER_CRITICAL(&schedMux_);
        s = sched_[i];
        portEXIT_CRITICAL(&schedMux_);
        if (!s.used) continue;

        const uint32_t flags =
            (s.def.enabled ? 0x01u : 0u) |
            (s.def.hasEnd ? 0x04u : 0u) |
            ((s.def.mode == TimeSchedulerMode::OneShotEpoch) ? 0x08u : 0u) |
            (s.def.replayStartOnBoot ? 0x10u : 0u);

        char label[TIME_SCHED_LABEL_MAX] = {0};
        strncpy(label, s.def.label, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        sanitizeLabel_(label);

        int n = snprintf(out + pos, outLen - pos,
                         "%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%s;",
                         (unsigned)s.def.slot,
                         (unsigned)s.def.eventId,
                         (unsigned)flags,
                         (unsigned)s.def.weekdayMask,
                         (unsigned)s.def.startHour,
                         (unsigned)s.def.startMinute,
                         (unsigned)s.def.endHour,
                         (unsigned)s.def.endMinute,
                         (unsigned long long)s.def.startEpochSec,
                         (unsigned long long)s.def.endEpochSec,
                         label);
        if (n <= 0 || (size_t)n >= (outLen - pos)) {
            out[outLen - 1] = '\0';
            return false;
        }
        pos += (size_t)n;
    }

    return true;
}

bool TimeModule::persistSchedule_()
{
    if (!cfgStore) return false;
    if (!serializeSchedule_(schedulePersistBuf_, sizeof(schedulePersistBuf_))) return false;
    return cfgStore->set(scheduleBlobVar, schedulePersistBuf_);
}

bool TimeModule::setSlot_(const TimeSchedulerSlot& slotDef)
{
    if (slotDef.slot >= TIME_SCHED_MAX_SLOTS) return false;
    if (isSystemSlot_(slotDef.slot)) return false;

    TimeSchedulerSlot normalized = slotDef;
    sanitizeLabel_(normalized.label);
    if (normalized.mode == TimeSchedulerMode::RecurringClock) {
        if (normalized.startHour > 23 || normalized.startMinute > 59) return false;
        if (normalized.hasEnd && (normalized.endHour > 23 || normalized.endMinute > 59)) return false;
        normalized.weekdayMask &= TIME_WEEKDAY_ALL;
        if (normalized.weekdayMask == 0) normalized.weekdayMask = TIME_WEEKDAY_ALL;
        normalized.startEpochSec = 0;
        normalized.endEpochSec = 0;
    } else {
        if (normalized.startEpochSec < 1609459200ULL) return false;
        if (normalized.hasEnd && normalized.endEpochSec <= normalized.startEpochSec) return false;
        normalized.weekdayMask = TIME_WEEKDAY_ALL;
        normalized.startHour = 0;
        normalized.startMinute = 0;
        normalized.endHour = 0;
        normalized.endMinute = 0;
    }

    portENTER_CRITICAL(&schedMux_);
    SchedulerSlotRuntime& s = sched_[normalized.slot];
    s.used = true;
    s.def = normalized;
    s.active = false;
    s.lastTriggerMinuteKey = INVALID_MINUTE_KEY;
    schedInitialized_ = false;
    activeMaskValue_ &= (uint16_t)~(uint16_t)(1u << normalized.slot);
    portEXIT_CRITICAL(&schedMux_);

    return persistSchedule_();
}

bool TimeModule::getSlot_(uint8_t slot, TimeSchedulerSlot& outDef) const
{
    if (slot >= TIME_SCHED_MAX_SLOTS) return false;

    bool ok = false;
    portENTER_CRITICAL(&schedMux_);
    const SchedulerSlotRuntime& s = sched_[slot];
    if (s.used) {
        outDef = s.def;
        ok = true;
    }
    portEXIT_CRITICAL(&schedMux_);
    return ok;
}

bool TimeModule::clearSlot_(uint8_t slot)
{
    if (slot >= TIME_SCHED_MAX_SLOTS) return false;
    if (isSystemSlot_(slot)) return false;

    portENTER_CRITICAL(&schedMux_);
    sched_[slot] = SchedulerSlotRuntime{};
    sched_[slot].def.slot = slot;
    activeMaskValue_ &= (uint16_t)~(uint16_t)(1u << slot);
    portEXIT_CRITICAL(&schedMux_);

    return persistSchedule_();
}

bool TimeModule::clearAllSlots_()
{
    portENTER_CRITICAL(&schedMux_);
    for (uint8_t i = 0; i < TIME_SCHED_MAX_SLOTS; ++i) {
        sched_[i] = SchedulerSlotRuntime{};
        sched_[i].def.slot = i;
    }
    applySystemSlots_(sched_, TIME_SCHED_MAX_SLOTS);
    activeMaskValue_ = 0;
    schedInitialized_ = false;
    portEXIT_CRITICAL(&schedMux_);
    return persistSchedule_();
}

uint8_t TimeModule::usedCount_() const
{
    uint8_t c = 0;
    portENTER_CRITICAL(&schedMux_);
    for (uint8_t i = 0; i < TIME_SCHED_MAX_SLOTS; ++i) {
        if (sched_[i].used) ++c;
    }
    portEXIT_CRITICAL(&schedMux_);
    return c;
}

uint16_t TimeModule::activeMask_() const
{
    uint16_t mask = 0;
    portENTER_CRITICAL(&schedMux_);
    mask = activeMaskValue_;
    portEXIT_CRITICAL(&schedMux_);
    return mask;
}

bool TimeModule::isActive_(uint8_t slot) const
{
    if (slot >= TIME_SCHED_MAX_SLOTS) return false;
    uint16_t mask = activeMask_();
    return (mask & (uint16_t)(1u << slot)) != 0;
}

void TimeModule::tickScheduler_()
{
    if (!eventBus) return;
    if (state != TimeSyncState::Synced) return;

    time_t now = nowEpoch_();
    if (now < (time_t)1609459200) return;

    struct tm localNow;
    if (!localtime_r(&now, &localNow)) return;

    const uint32_t minuteKey = (uint32_t)(((uint64_t)now) / 60ULL);
    const uint8_t weekBit = weekBitFromTm_(localNow);
    const uint8_t prevWeekBit = (weekBit == 0) ? 6u : (uint8_t)(weekBit - 1u);
    const uint32_t dayMinute = minuteOfDay_(localNow);

    struct PendingEvent {
        uint8_t slot;
        uint8_t edge;
        uint8_t replayed;
        uint16_t eventId;
        uint64_t epochSec;
    };

    PendingEvent pending[TIME_SCHED_MAX_SLOTS * 2]{};
    uint8_t pendingCount = 0;

    auto pushPending = [&](uint8_t slot, SchedulerEdge edge, uint8_t replayed, uint16_t eventId) {
        if (pendingCount >= (TIME_SCHED_MAX_SLOTS * 2)) return;
        pending[pendingCount++] = PendingEvent{slot, (uint8_t)edge, replayed, eventId, (uint64_t)now};
    };

    portENTER_CRITICAL(&schedMux_);

    uint16_t newMask = 0;

    for (uint8_t i = 0; i < TIME_SCHED_MAX_SLOTS; ++i) {
        SchedulerSlotRuntime& s = sched_[i];
        if (!s.used) continue;

        if (!s.def.enabled) {
            if (s.active) {
                s.active = false;
                pushPending(i, SchedulerEdge::Stop, 0, s.def.eventId);
            }
            continue;
        }

        if (s.def.mode == TimeSchedulerMode::OneShotEpoch) {
            if (!s.def.hasEnd) {
                if ((uint64_t)now >= s.def.startEpochSec) {
                    if (s.lastTriggerMinuteKey != minuteKey) {
                        const uint8_t replayed = schedInitialized_ ? 0 : 1;
                        pushPending(i, SchedulerEdge::Trigger, replayed, s.def.eventId);
                        s.lastTriggerMinuteKey = minuteKey;
                    }
                    s.used = false;
                    s.active = false;
                }
                continue;
            }

            bool activeNow = ((uint64_t)now >= s.def.startEpochSec) && ((uint64_t)now < s.def.endEpochSec);

            if (!schedInitialized_) {
                s.active = activeNow;
                if (activeNow && s.def.replayStartOnBoot) {
                    pushPending(i, SchedulerEdge::Start, 1, s.def.eventId);
                }
            } else {
                if (!s.active && activeNow) {
                    pushPending(i, SchedulerEdge::Start, 0, s.def.eventId);
                } else if (s.active && !activeNow) {
                    pushPending(i, SchedulerEdge::Stop, 0, s.def.eventId);
                }
                s.active = activeNow;
            }

            if (!s.active && (uint64_t)now >= s.def.endEpochSec) {
                s.used = false;
            } else if (s.active) {
                newMask |= (uint16_t)(1u << i);
            }
            continue;
        }

        // Recurring clock mode
        if (!s.def.hasEnd) {
            if (isRecurringTriggerNow_(s.def, weekBit, dayMinute)) {
                if (s.def.slot == TIME_SLOT_SYS_MONTH_START && !isMonthStartEvent_(s, localNow)) {
                    continue;
                }
                if (s.lastTriggerMinuteKey != minuteKey) {
                    const uint8_t replayed = schedInitialized_ ? 0 : 1;
                    pushPending(i, SchedulerEdge::Trigger, replayed, s.def.eventId);
                    s.lastTriggerMinuteKey = minuteKey;
                }
            }
            s.active = false;
            continue;
        }

        bool activeNow = isRecurringActiveNow_(s.def, weekBit, prevWeekBit, dayMinute);

        if (!schedInitialized_) {
            s.active = activeNow;
            if (activeNow && s.def.replayStartOnBoot) {
                pushPending(i, SchedulerEdge::Start, 1, s.def.eventId);
            }
        } else {
            if (!s.active && activeNow) {
                pushPending(i, SchedulerEdge::Start, 0, s.def.eventId);
            } else if (s.active && !activeNow) {
                pushPending(i, SchedulerEdge::Stop, 0, s.def.eventId);
            }
            s.active = activeNow;
        }

        if (s.active) {
            newMask |= (uint16_t)(1u << i);
        }
    }

    activeMaskValue_ = newMask;
    schedInitialized_ = true;

    portEXIT_CRITICAL(&schedMux_);

    for (uint8_t i = 0; i < pendingCount; ++i) {
        SchedulerEventTriggeredPayload payload{};
        payload.slot = pending[i].slot;
        payload.edge = pending[i].edge;
        payload.replayed = pending[i].replayed;
        payload.eventId = pending[i].eventId;
        payload.epochSec = pending[i].epochSec;
        payload.activeMask = activeMask_();

        LOGI("Scheduler event %s slot=%u eventId=%u replayed=%u activeMask=0x%04X epoch=%llu",
             schedulerEdgeStr(payload.edge),
             (unsigned)payload.slot,
             (unsigned)payload.eventId,
             (unsigned)payload.replayed,
             (unsigned)payload.activeMask,
             (unsigned long long)payload.epochSec);

        (void)eventBus->post(EventId::SchedulerEventTriggered, &payload, sizeof(payload));
    }
}
