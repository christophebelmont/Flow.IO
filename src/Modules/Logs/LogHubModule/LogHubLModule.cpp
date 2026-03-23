/**
 * @file LogHubLModule.cpp
 * @brief Implementation file.
 */
#include "LogHubModule.h"
#include "Core/Log.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/LogModuleIds.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::LogHub)
#include "Core/ModuleLog.h"
#include "Core/SystemLimits.h"
#include <new>

namespace {
static constexpr uint8_t kLogHubCfgProducerId = 52;
static constexpr uint8_t kLogLevelsCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kLogCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Log, kLogLevelsCfgBranch}, "log/levels", "log/levels", (uint8_t)MqttPublishPriority::Normal, nullptr},
};

bool hubEnqueue(void* ctx, const LogEntry& e)
{
    return static_cast<LogHub*>(ctx)->enqueue(e);
}

bool hubRegisterModule(void* ctx, LogModuleId moduleId, const char* moduleName)
{
    return static_cast<LogHub*>(ctx)->registerModule(moduleId, moduleName);
}

bool hubShouldLog(void* ctx, LogModuleId moduleId, LogLevel level)
{
    return static_cast<const LogHub*>(ctx)->shouldLog(moduleId, level);
}

const char* hubResolveModuleName(void* ctx, LogModuleId moduleId)
{
    return static_cast<const LogHub*>(ctx)->resolveModuleName(moduleId);
}

bool hubSetModuleMinLevel(void* ctx, LogModuleId moduleId, LogLevel level)
{
    return static_cast<LogHub*>(ctx)->setModuleMinLevel(moduleId, level);
}

LogLevel hubGetModuleMinLevel(void* ctx, LogModuleId moduleId)
{
    return static_cast<const LogHub*>(ctx)->getModuleMinLevel(moduleId);
}

void hubGetStats(void* ctx, LogHubStatsSnapshot* out)
{
    if (!out) return;
    static_cast<const LogHub*>(ctx)->getStats(*out);
}

void hubNoteFormatTruncation(void* ctx, LogModuleId moduleId, uint32_t wrote)
{
    static_cast<LogHub*>(ctx)->noteFormatTruncation(moduleId, wrote);
}

bool sinkAdd(void* ctx, LogSinkService sink)
{
    return static_cast<LogSinkRegistry*>(ctx)->add(sink);
}

int sinkCount(void* ctx)
{
    return static_cast<const LogSinkRegistry*>(ctx)->count();
}

LogSinkService sinkGet(void* ctx, int index)
{
    return static_cast<const LogSinkRegistry*>(ctx)->get(index);
}
}

void LogHubModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    hub.init(Limits::LogQueueLen);
    hub.attachConfig(&cfg, (uint8_t)ConfigModuleId::Log, kLogLevelsCfgBranch);

    // LogDispatcher consumes hubSvc_.ctx as a raw LogHub* for dequeue().
    hubSvc_.enqueue = &hubEnqueue;
    hubSvc_.registerModule = &hubRegisterModule;
    hubSvc_.shouldLog = &hubShouldLog;
    hubSvc_.resolveModuleName = &hubResolveModuleName;
    hubSvc_.setModuleMinLevel = &hubSetModuleMinLevel;
    hubSvc_.getModuleMinLevel = &hubGetModuleMinLevel;
    hubSvc_.getStats = &hubGetStats;
    hubSvc_.noteFormatTruncation = &hubNoteFormatTruncation;
    hubSvc_.ctx = &hub;

    sinksSvc_.add = &sinkAdd;
    sinksSvc_.count = &sinkCount;
    sinksSvc_.get = &sinkGet;
    sinksSvc_.ctx = &sinks;

    if (!services.add(ServiceId::LogHub, &hubSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::LogHub));
    }
    if (!services.add(ServiceId::LogSinks, &sinksSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::LogSinks));
    }

    Log::setHub(&hubSvc_);
    (void)Log::registerModule((LogModuleId)LogModuleIdValue::LogHub, toString(moduleId()));
    (void)Log::registerModule((LogModuleId)LogModuleIdValue::CoreI2cLink, "core.i2clink");
    (void)Log::registerModule((LogModuleId)LogModuleIdValue::CoreModuleManager, "core.modulemanager");
    (void)Log::registerModule((LogModuleId)LogModuleIdValue::CoreConfigStore, "core.configstore");
    (void)Log::registerModule((LogModuleId)LogModuleIdValue::CoreEventBus, "core.eventbus");
}

void LogHubModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kLogHubCfgProducerId,
                               kLogCfgRoutes,
                               (uint8_t)(sizeof(kLogCfgRoutes) / sizeof(kLogCfgRoutes[0])),
                               services);
    }
}
