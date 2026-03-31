#pragma once
/**
 * @file LogHub.h
 * @brief Central log queue for asynchronous logging.
 */
#include "Core/ConfigTypes.h"
#include "Core/Services/ILogger.h"
#include "Core/SystemLimits.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class ConfigStore;

/**
 * @brief Queue-based log hub for producers and consumers.
 */
class LogHub {
public:
    /** @brief Initialize the log queue with a given length. */
    void init(int queueLen = Limits::LogQueueLen);

    /** @brief Enqueue a log entry (non-blocking). */
    bool enqueue(const LogEntry& e);
    /** @brief Dequeue a log entry (blocking up to waitTicks). */
    bool dequeue(LogEntry& out, TickType_t waitTicks);

    /** @brief Attach config store used to expose per-module minimum levels. */
    void attachConfig(ConfigStore* cfg, uint8_t cfgModuleId, uint8_t cfgLocalBranchId);

    /** @brief Register or update a module descriptor. */
    bool registerModule(LogModuleId moduleId, const char* moduleName);
    /** @brief Return true when the log level should be emitted for the module. */
    bool shouldLog(LogModuleId moduleId, LogLevel level) const;
    /** @brief Resolve module id to registered name (or static fallback). */
    const char* resolveModuleName(LogModuleId moduleId) const;
    /** @brief Override a module minimum level. */
    bool setModuleMinLevel(LogModuleId moduleId, LogLevel level);
    /** @brief Read a module minimum level. */
    LogLevel getModuleMinLevel(LogModuleId moduleId) const;
    /** @brief Snapshot queue/truncation counters for diagnostics. */
    void getStats(LogHubStatsSnapshot& out) const;
    /** @brief Record a message formatting truncation before enqueue. */
    void noteFormatTruncation(LogModuleId moduleId, uint32_t wrote);

private:
    static constexpr LogLevel kDefaultMinLevel = LogLevel::Info;

    struct ModuleRegistration {
        LogModuleId id = (LogModuleId)LogModuleIdValue::Unknown;
        char name[24] = {0};
        int32_t minLevelRaw = (int32_t)kDefaultMinLevel;
        char nvsKey[Limits::MaxNvsKeyLen + 1] = {0};
        char jsonName[20] = {0};
        ConfigVariable<int32_t, 0> minLevelVar{};
        bool cfgRegistered = false;
    };

    static constexpr uint8_t MAX_REGISTERED_MODULES = 40;
    static LogLevel clampLevel_(int32_t rawLevel);
    ModuleRegistration* findModule_(LogModuleId moduleId);
    const ModuleRegistration* findModule_(LogModuleId moduleId) const;
    bool registerConfigVar_(ModuleRegistration& slot);

    QueueHandle_t q = nullptr;
    uint16_t queueLen_ = Limits::LogQueueLen;
    mutable portMUX_TYPE statsMux_ = portMUX_INITIALIZER_UNLOCKED;
    uint16_t peakQueued_ = 0;
    uint32_t enqueuedCount_ = 0;
    uint32_t droppedCount_ = 0;
    uint32_t formatTruncCount_ = 0;
    uint32_t lastDropMs_ = 0;
    uint32_t lastFormatTruncMs_ = 0;
    LogModuleId lastDropModuleId_ = (LogModuleId)LogModuleIdValue::Unknown;
    LogModuleId lastFormatTruncModuleId_ = (LogModuleId)LogModuleIdValue::Unknown;
    ModuleRegistration modules_[MAX_REGISTERED_MODULES]{};
    uint8_t moduleCount_ = 0;
    ConfigStore* cfg_ = nullptr;
    uint8_t cfgModuleId_ = 0;
    uint8_t cfgLocalBranchId_ = 0;
};
