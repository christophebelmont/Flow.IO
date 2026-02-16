#pragma once
/**
 * @file AlarmModule.h
 * @brief Central alarm registry/evaluation engine.
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/SystemLimits.h"
#include "Core/Services/Services.h"

struct CommandRequest;

class AlarmModule : public Module {
public:
    const char* moduleId() const override { return "alarms"; }
    const char* taskName() const override { return "alarms"; }

    uint8_t dependencyCount() const override { return 3; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "eventbus";
        if (i == 2) return "cmd";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;
    void loop() override;

private:
    struct AlarmSlot {
        bool used = false;
        AlarmId id = AlarmId::None;
        AlarmRegistration def{};
        AlarmCondFn condFn = nullptr;
        void* condCtx = nullptr;

        bool active = false;
        bool acked = false;
        AlarmCondState lastCond = AlarmCondState::Unknown;
        uint32_t onSinceMs = 0;
        uint32_t offSinceMs = 0;
        uint32_t activeSinceMs = 0;
        uint32_t lastChangeMs = 0;
    };

    static bool svcRegisterAlarm_(void* ctx, const AlarmRegistration* def, AlarmCondFn condFn, void* condCtx);
    static bool svcAck_(void* ctx, AlarmId id);
    static uint8_t svcAckAll_(void* ctx);
    static bool svcIsActive_(void* ctx, AlarmId id);
    static bool svcIsAcked_(void* ctx, AlarmId id);
    static uint8_t svcActiveCount_(void* ctx);
    static AlarmSeverity svcHighestSeverity_(void* ctx);
    static bool svcBuildSnapshot_(void* ctx, char* out, size_t len);

    static bool cmdList_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdAck_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdAckAll_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool registerAlarm_(const AlarmRegistration& def, AlarmCondFn condFn, void* condCtx);
    bool ack_(AlarmId id);
    uint8_t ackAll_();
    bool isActive_(AlarmId id) const;
    bool isAcked_(AlarmId id) const;
    uint8_t activeCount_() const;
    AlarmSeverity highestSeverity_() const;
    bool buildSnapshot_(char* out, size_t len) const;
    bool handleCmdAck_(const CommandRequest& req, char* reply, size_t replyLen);
    void evaluateOnce_(uint32_t nowMs);

    int16_t findSlotById_(AlarmId id) const;
    int16_t findFreeSlot_() const;
    void emitAlarmEvent_(EventId id, AlarmId alarmId) const;
    static bool delayReached_(uint32_t sinceMs, uint32_t delayMs, uint32_t nowMs);
    static const char* condStateStr_(AlarmCondState s);

    AlarmService alarmSvc_{
        svcRegisterAlarm_,
        svcAck_,
        svcAckAll_,
        svcIsActive_,
        svcIsAcked_,
        svcActiveCount_,
        svcHighestSeverity_,
        svcBuildSnapshot_,
        this
    };

    const LogHubService* logHub_ = nullptr;
    EventBus* eventBus_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;

    bool enabled_ = true;
    int32_t evalPeriodMsCfg_ = (int32_t)Limits::Alarm::DefaultEvalPeriodMs;

    ConfigVariable<bool,0> enabledVar_{
        NVS_KEY(NvsKeys::Alarm::Enabled), "enabled", "alarms", ConfigType::Bool,
        &enabled_, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> evalPeriodVar_{
        NVS_KEY(NvsKeys::Alarm::EvalPeriodMs), "eval_period_ms", "alarms", ConfigType::Int32,
        &evalPeriodMsCfg_, ConfigPersistence::Persistent, 0
    };

    mutable portMUX_TYPE slotsMux_ = portMUX_INITIALIZER_UNLOCKED;
    AlarmSlot slots_[Limits::Alarm::MaxAlarms]{};
};
