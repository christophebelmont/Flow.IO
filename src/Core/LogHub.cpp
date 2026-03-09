/**
 * @file LogHub.cpp
 * @brief Implementation file.
 */
#include "Core/LogHub.h"
#include "Core/ConfigStore.h"
#include "Core/LogModuleIds.h"
#include <stdio.h>
#include <string.h>

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
    slot.cfgRegistered = true;
    return true;
}

void LogHub::init(int queueLen) {
    q = xQueueCreate(queueLen, sizeof(LogEntry));
}

bool LogHub::enqueue(const LogEntry& e) {
    if (!q) return false;
    return xQueueSend(q, &e, 0) == pdTRUE;  ///< 0 => non bloquant
}

bool LogHub::dequeue(LogEntry& out, TickType_t waitTicks) {
    if (!q) return false;
    return xQueueReceive(q, &out, waitTicks) == pdTRUE;
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
        slot->minLevelRaw = (int32_t)LogLevel::Debug;
    }

    strncpy(slot->name, moduleName, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
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
    if (!slot) return LogLevel::Debug;
    return clampLevel_(slot->minLevelRaw);
}
