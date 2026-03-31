/**
 * @file LogHub.cpp
 * @brief Implementation file.
 */
#include "Core/LogHub.h"
#include "Core/BufferUsageTracker.h"
#include "Core/ConfigStore.h"
#include "Core/LogModuleIds.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {
static bool shouldExposeConfigLevel_(LogModuleId moduleId)
{
#if defined(FLOW_PROFILE_SUPERVISOR)
    switch ((LogModuleIdValue)moduleId) {
        case LogModuleIdValue::LogHub:
        case LogModuleIdValue::LogDispatcher:
        case LogModuleIdValue::EventBusModule:
        case LogModuleIdValue::ConfigStoreModule:
        case LogModuleIdValue::CommandModule:
        case LogModuleIdValue::WifiModule:
        case LogModuleIdValue::WifiProvisioningModule:
        case LogModuleIdValue::TimeModule:
        case LogModuleIdValue::I2cCfgClientModule:
        case LogModuleIdValue::WebInterfaceModule:
        case LogModuleIdValue::FirmwareUpdateModule:
        case LogModuleIdValue::SystemModule:
        case LogModuleIdValue::SystemMonitorModule:
        case LogModuleIdValue::HMIModule:
        case LogModuleIdValue::CoreI2cLink:
        case LogModuleIdValue::CoreModuleManager:
        case LogModuleIdValue::CoreConfigStore:
        case LogModuleIdValue::CoreEventBus:
        case LogModuleIdValue::LogSinkSerial:
        case LogModuleIdValue::LogSinkAlarm:
            return true;
        default:
            return false;
    }
#else
    switch ((LogModuleIdValue)moduleId) {
        case LogModuleIdValue::LogHub:
        case LogModuleIdValue::LogDispatcher:
        case LogModuleIdValue::EventBusModule:
        case LogModuleIdValue::ConfigStoreModule:
        case LogModuleIdValue::AlarmModule:
        case LogModuleIdValue::WifiModule:
        case LogModuleIdValue::TimeModule:
        case LogModuleIdValue::I2cCfgServerModule:
        case LogModuleIdValue::SystemModule:
        case LogModuleIdValue::SystemMonitorModule:
        case LogModuleIdValue::HAModule:
        case LogModuleIdValue::MQTTModule:
        case LogModuleIdValue::IOModule:
        case LogModuleIdValue::PoolDeviceModule:
        case LogModuleIdValue::PoolLogicModule:
        case LogModuleIdValue::HMIModule:
        case LogModuleIdValue::CoreI2cLink:
        case LogModuleIdValue::CoreModuleManager:
        case LogModuleIdValue::CoreConfigStore:
        case LogModuleIdValue::CoreEventBus:
            return true;
        default:
            return false;
    }
#endif
}
}  // namespace

LogLevel LogHub::clampLevel_(int32_t rawLevel)
{
    if (rawLevel <= (int32_t)LogLevel::Debug) return LogLevel::Debug;
    if (rawLevel == (int32_t)LogLevel::Info) return LogLevel::Info;
    if (rawLevel == (int32_t)LogLevel::Warn) return LogLevel::Warn;
    return LogLevel::Error;
}

LogHub::ModuleRegistration* LogHub::findModule_(LogModuleId moduleId)
{
    for (uint8_t i = 0; i < moduleCount_; ++i) {
        if (modules_[i].id == moduleId) return &modules_[i];
    }
    return nullptr;
}

const LogHub::ModuleRegistration* LogHub::findModule_(LogModuleId moduleId) const
{
    for (uint8_t i = 0; i < moduleCount_; ++i) {
        if (modules_[i].id == moduleId) return &modules_[i];
    }
    return nullptr;
}

bool LogHub::registerConfigVar_(ModuleRegistration& slot)
{
    if (!cfg_ || slot.cfgRegistered) return false;
    if (!shouldExposeConfigLevel_(slot.id)) {
        slot.cfgRegistered = true;
        return false;
    }

    snprintf(slot.nvsKey, sizeof(slot.nvsKey), "lgm%03u", (unsigned)slot.id);
    snprintf(slot.jsonName, sizeof(slot.jsonName), "m%u_lvl", (unsigned)slot.id);

    slot.minLevelVar.nvsKey = slot.nvsKey;
    slot.minLevelVar.jsonName = slot.jsonName;
    slot.minLevelVar.moduleName = "log/levels";
    slot.minLevelVar.type = ConfigType::Int32;
    slot.minLevelVar.value = &slot.minLevelRaw;
    slot.minLevelVar.persistence = ConfigPersistence::Persistent;
    slot.minLevelVar.size = 0;

    cfg_->registerVar(slot.minLevelVar, cfgModuleId_, cfgLocalBranchId_);
    cfg_->loadPersistentVar(slot.minLevelVar);
    slot.cfgRegistered = true;
    return true;
}

void LogHub::init(int queueLen) {
    queueLen_ = (queueLen > 0) ? (uint16_t)queueLen : Limits::LogQueueLen;
    q = xQueueCreate(queueLen, sizeof(LogEntry));
    portENTER_CRITICAL(&statsMux_);
    peakQueued_ = 0;
    enqueuedCount_ = 0;
    droppedCount_ = 0;
    formatTruncCount_ = 0;
    lastDropMs_ = 0;
    lastFormatTruncMs_ = 0;
    lastDropModuleId_ = (LogModuleId)LogModuleIdValue::Unknown;
    lastFormatTruncModuleId_ = (LogModuleId)LogModuleIdValue::Unknown;
    portEXIT_CRITICAL(&statsMux_);
    BufferUsageTracker::note(TrackedBufferId::LogHubQueue, 0U, (size_t)queueLen_ * sizeof(LogEntry), "init", nullptr);
}

bool LogHub::enqueue(const LogEntry& e) {
    if (!q) return false;
    const bool ok = xQueueSend(q, &e, 0) == pdTRUE;  ///< 0 => non bloquant
    const UBaseType_t queued = q ? uxQueueMessagesWaiting(q) : 0U;
    portENTER_CRITICAL(&statsMux_);
    if (queued > peakQueued_) peakQueued_ = (uint16_t)queued;
    if (ok) {
        ++enqueuedCount_;
    } else {
        ++droppedCount_;
        lastDropMs_ = e.ts_ms;
        lastDropModuleId_ = e.moduleId;
        peakQueued_ = queueLen_;
    }
    portEXIT_CRITICAL(&statsMux_);
    BufferUsageTracker::note(TrackedBufferId::LogHubQueue,
                             (size_t)queued * sizeof(LogEntry),
                             (size_t)queueLen_ * sizeof(LogEntry),
                             logModuleNameFromId(e.moduleId),
                             nullptr);
    return ok;
}

bool LogHub::dequeue(LogEntry& out, TickType_t waitTicks) {
    if (!q) return false;
    const bool ok = xQueueReceive(q, &out, waitTicks) == pdTRUE;
    const UBaseType_t queued = q ? uxQueueMessagesWaiting(q) : 0U;
    BufferUsageTracker::note(TrackedBufferId::LogHubQueue,
                             (size_t)queued * sizeof(LogEntry),
                             (size_t)queueLen_ * sizeof(LogEntry),
                             ok ? "dequeue" : "idle",
                             nullptr);
    return ok;
}

void LogHub::attachConfig(ConfigStore* cfg, uint8_t cfgModuleId, uint8_t cfgLocalBranchId)
{
    cfg_ = cfg;
    cfgModuleId_ = cfgModuleId;
    cfgLocalBranchId_ = cfgLocalBranchId;
    if (!cfg_) return;

    for (uint8_t i = 0; i < moduleCount_; ++i) {
        (void)registerConfigVar_(modules_[i]);
    }
}

bool LogHub::registerModule(LogModuleId moduleId, const char* moduleName)
{
    if (moduleId == (LogModuleId)LogModuleIdValue::Unknown) return false;
    if (!moduleName || moduleName[0] == '\0') return false;

    ModuleRegistration* slot = findModule_(moduleId);
    if (!slot) {
        if (moduleCount_ >= MAX_REGISTERED_MODULES) return false;
        slot = &modules_[moduleCount_++];
        slot->id = moduleId;
        slot->minLevelRaw = (int32_t)kDefaultMinLevel;
    }

    strncpy(slot->name, moduleName, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    BufferUsageTracker::note(TrackedBufferId::LogHubModules,
                             (size_t)moduleCount_ * sizeof(ModuleRegistration),
                             sizeof(modules_),
                             moduleName,
                             nullptr);
    if (cfg_) (void)registerConfigVar_(*slot);
    return true;
}

bool LogHub::shouldLog(LogModuleId moduleId, LogLevel level) const
{
    const ModuleRegistration* slot = findModule_(moduleId);
    if (!slot) return true;
    return (int32_t)level >= (int32_t)clampLevel_(slot->minLevelRaw);
}

const char* LogHub::resolveModuleName(LogModuleId moduleId) const
{
    const ModuleRegistration* slot = findModule_(moduleId);
    if (slot && slot->name[0] != '\0') return slot->name;
    return logModuleNameFromId(moduleId);
}

bool LogHub::setModuleMinLevel(LogModuleId moduleId, LogLevel level)
{
    ModuleRegistration* slot = findModule_(moduleId);
    if (!slot) return false;
    slot->minLevelRaw = (int32_t)level;
    return true;
}

LogLevel LogHub::getModuleMinLevel(LogModuleId moduleId) const
{
    const ModuleRegistration* slot = findModule_(moduleId);
    if (!slot) return kDefaultMinLevel;
    return clampLevel_(slot->minLevelRaw);
}

void LogHub::getStats(LogHubStatsSnapshot& out) const
{
    out = {};
    out.queueLen = queueLen_;
    out.queuedNow = q ? (uint16_t)uxQueueMessagesWaiting(q) : 0U;
    portENTER_CRITICAL(&statsMux_);
    out.peakQueued = peakQueued_;
    out.enqueuedCount = enqueuedCount_;
    out.droppedCount = droppedCount_;
    out.formatTruncCount = formatTruncCount_;
    out.lastDropMs = lastDropMs_;
    out.lastFormatTruncMs = lastFormatTruncMs_;
    out.lastDropModuleId = lastDropModuleId_;
    out.lastFormatTruncModuleId = lastFormatTruncModuleId_;
    portEXIT_CRITICAL(&statsMux_);
}

void LogHub::noteFormatTruncation(LogModuleId moduleId, uint32_t wrote)
{
    (void)wrote;
    portENTER_CRITICAL(&statsMux_);
    ++formatTruncCount_;
    lastFormatTruncMs_ = millis();
    lastFormatTruncModuleId_ = moduleId;
    portEXIT_CRITICAL(&statsMux_);
}
