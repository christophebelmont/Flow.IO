#pragma once
/**
 * @file TimeModule.h
 * @brief Time synchronization and scheduling module.
 */
#include "Core/Module.h"
#include "Core/Services/Services.h"
#include <time.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>

/** @brief Time sync configuration values. */
struct TimeConfig {
    // Current backend is NTP. The module contract is intentionally generic
    // to allow future backend extensions (RTC, external time source, ...).
    char server1[40] = "pool.ntp.org";
    char server2[40] = "time.nist.gov";
    char tz[64]      = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    bool enabled = true;
    bool weekStartMonday = true;
};

/**
 * @brief Active module that synchronizes time and drives scheduler events.
 */
class TimeModule : public Module {
public:
    /** @brief Module id. */
    const char* moduleId() const override { return "time"; }
    /** @brief Task name. */
    const char* taskName() const override { return "time"; }

    /** @brief Depends on log hub, datastore, command and event bus. */
    uint8_t dependencyCount() const override { return 4; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "datastore";
        if (i == 2) return "cmd";
        if (i == 3) return "eventbus";
        return nullptr;
    }

    /** @brief Initialize time config and services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Time task loop. */
    void loop() override;

    /** @brief Force a resync attempt. */
    void forceResync();

private:
    static constexpr uint32_t INVALID_MINUTE_KEY = 0xFFFFFFFFUL;
    static constexpr size_t TIME_SCHED_BLOB_SIZE = 1536;

    struct SchedulerSlotRuntime {
        bool used = false;
        TimeSchedulerSlot def{};
        bool active = false;
        uint32_t lastTriggerMinuteKey = INVALID_MINUTE_KEY;
    };

    TimeConfig cfgData{};
    char scheduleBlob_[TIME_SCHED_BLOB_SIZE] = {0};
    char schedulePersistBuf_[TIME_SCHED_BLOB_SIZE] = {0};

    ConfigStore* cfgStore = nullptr;
    const CommandService* cmdSvc = nullptr;
    const LogHubService* logHub = nullptr;
    EventBus* eventBus = nullptr;
    DataStore* dataStore = nullptr;

    static void onEventStatic(const Event& e, void* user);
    void onEvent(const Event& e);

    static bool cmdResync(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedInfo(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedGet(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedSet(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedClear(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSchedClearAll(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool handleCmdSchedInfo_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedGet_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedSet_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedClear_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdSchedClearAll_(const CommandRequest& req, char* reply, size_t replyLen);

    TimeSyncState state = TimeSyncState::WaitingNetwork;
    uint32_t stateTs = 0;

    // Keep existing NVS keys for backward compatibility with deployed devices.
    ConfigVariable<char,0> server1Var {
        NVS_KEY("ntp_s1"),"server1","time",ConfigType::CharArray,
        (char*)cfgData.server1,ConfigPersistence::Persistent,sizeof(cfgData.server1)
    };
    ConfigVariable<char,0> server2Var {
        NVS_KEY("ntp_s2"),"server2","time",ConfigType::CharArray,
        (char*)cfgData.server2,ConfigPersistence::Persistent,sizeof(cfgData.server2)
    };
    ConfigVariable<char,0> tzVar {
        NVS_KEY("ntp_tz"),"tz","time",ConfigType::CharArray,
        (char*)cfgData.tz,ConfigPersistence::Persistent,sizeof(cfgData.tz)
    };
    ConfigVariable<bool,0> enabledVar {
        NVS_KEY("ntp_en"),"enabled","time",ConfigType::Bool,
        &cfgData.enabled,ConfigPersistence::Persistent,0
    };
    ConfigVariable<bool,0> weekStartMondayVar {
        NVS_KEY("tm_wkmon"),"week_start_monday","time",ConfigType::Bool,
        &cfgData.weekStartMonday,ConfigPersistence::Persistent,0
    };
    ConfigVariable<char,0> scheduleBlobVar {
        NVS_KEY("tm_sched"),"slots_blob","time/scheduler",ConfigType::CharArray,
        (char*)scheduleBlob_,ConfigPersistence::Persistent,sizeof(scheduleBlob_)
    };

    void setState(TimeSyncState s);

    static TimeSyncState svcState(void* ctx);
    static bool svcIsSynced(void* ctx);
    static uint64_t svcEpoch(void* ctx);
    static bool svcFormatLocalTime(void* ctx, char* out, size_t len);

    static bool svcSchedSetSlot(void* ctx, const TimeSchedulerSlot* slotDef);
    static bool svcSchedGetSlot(void* ctx, uint8_t slot, TimeSchedulerSlot* outDef);
    static bool svcSchedClearSlot(void* ctx, uint8_t slot);
    static bool svcSchedClearAll(void* ctx);
    static uint8_t svcSchedUsedCount(void* ctx);
    static uint16_t svcSchedActiveMask(void* ctx);
    static bool svcSchedIsActive(void* ctx, uint8_t slot);

    bool setSlot_(const TimeSchedulerSlot& slotDef);
    bool getSlot_(uint8_t slot, TimeSchedulerSlot& outDef) const;
    bool clearSlot_(uint8_t slot);
    bool clearAllSlots_();
    uint8_t usedCount_() const;
    uint16_t activeMask_() const;
    bool isActive_(uint8_t slot) const;

    bool loadScheduleFromBlob_();
    bool serializeSchedule_(char* out, size_t outLen) const;
    bool persistSchedule_();
    time_t nowEpoch_() const;
    static void sanitizeLabel_(char* label);
    void applySystemSlots_(SchedulerSlotRuntime* slots, size_t count) const;
    bool isSystemSlot_(uint8_t slot) const;
    bool isMonthStartEvent_(const SchedulerSlotRuntime& slotRt, const tm& localNow) const;
    void resetScheduleRuntime_();
    void tickScheduler_();

    static uint8_t weekBitFromTm_(const tm& localNow);
    static uint32_t minuteOfDay_(const tm& localNow);
    static bool isWeekdayEnabled_(uint8_t mask, uint8_t weekBit);
    static bool isRecurringTriggerNow_(const TimeSchedulerSlot& def, uint8_t weekBit, uint32_t minuteOfDay);
    static bool isRecurringActiveNow_(const TimeSchedulerSlot& def, uint8_t weekBit, uint8_t prevWeekBit,
                                      uint32_t minuteOfDay);

    // ---- network warmup ----
    bool _netReady = false;
    uint32_t _netReadyTs = 0;

    // ---- retry backoff ----
    uint8_t _retryCount = 0;
    uint32_t _retryDelayMs = 2000; // 2s start

    // ---- time scheduler ----
    mutable portMUX_TYPE schedMux_ = portMUX_INITIALIZER_UNLOCKED;
    SchedulerSlotRuntime sched_[TIME_SCHED_MAX_SLOTS]{};
    bool schedNeedsReload_ = true;
    bool schedInitialized_ = false;
    uint16_t activeMaskValue_ = 0;
    uint32_t simBootMs_ = 0;
};
