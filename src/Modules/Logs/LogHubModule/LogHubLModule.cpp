/**
 * @file LogHubLModule.cpp
 * @brief Implementation file.
 */
#include "LogHubModule.h"
#include "Core/Log.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/LogModuleIds.h"
#include "Core/SystemLimits.h"
#include <new>

namespace {
static constexpr uint8_t kLogHubCfgProducerId = 52;
static constexpr uint8_t kLogLevelsCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kLogCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Log, kLogLevelsCfgBranch}, "log/levels", "log/levels", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
}

void LogHubModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    hub.init(Limits::LogQueueLen);
    hub.attachConfig(&cfg, (uint8_t)ConfigModuleId::Log, kLogLevelsCfgBranch);

    /// expose loghub service
    hubSvc.enqueue = [](void* ctx, const LogEntry& e) -> bool {
        return static_cast<LogHub*>(ctx)->enqueue(e);
    };
    hubSvc.registerModule = [](void* ctx, LogModuleId moduleId, const char* moduleName) -> bool {
        return static_cast<LogHub*>(ctx)->registerModule(moduleId, moduleName);
    };
    hubSvc.shouldLog = [](void* ctx, LogModuleId moduleId, LogLevel level) -> bool {
        return static_cast<LogHub*>(ctx)->shouldLog(moduleId, level);
    };
    hubSvc.resolveModuleName = [](void* ctx, LogModuleId moduleId) -> const char* {
        return static_cast<const LogHub*>(ctx)->resolveModuleName(moduleId);
    };
    hubSvc.setModuleMinLevel = [](void* ctx, LogModuleId moduleId, LogLevel level) -> bool {
        return static_cast<LogHub*>(ctx)->setModuleMinLevel(moduleId, level);
    };
    hubSvc.getModuleMinLevel = [](void* ctx, LogModuleId moduleId) -> LogLevel {
        return static_cast<const LogHub*>(ctx)->getModuleMinLevel(moduleId);
    };
    hubSvc.ctx = &hub;

    /// expose sink registry service
    sinksSvc.add = [](void* ctx, LogSinkService sink) -> bool {
        return static_cast<LogSinkRegistry*>(ctx)->add(sink);
    };
    sinksSvc.count = [](void* ctx) -> int {
        return static_cast<LogSinkRegistry*>(ctx)->count();
    };
    sinksSvc.get = [](void* ctx, int idx) -> LogSinkService {
        return static_cast<LogSinkRegistry*>(ctx)->get(idx);
    };
    sinksSvc.ctx = &sinks;

    services.add("loghub", &hubSvc);
    services.add("logsinks", &sinksSvc);

    Log::setHub(&hubSvc);
    (void)Log::registerModule((LogModuleId)LogModuleIdValue::LogHub, moduleId());
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
