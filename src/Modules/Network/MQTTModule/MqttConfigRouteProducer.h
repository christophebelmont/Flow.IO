#pragma once

#include "Core/EventBus/EventPayloads.h"
#include "Core/ConfigBranchRef.h"
#include "Core/ServiceRegistry.h"
#include "Core/Services/Services.h"
#include <stddef.h>

/**
 * @brief Reusable config MQTT producer for module-owned cfg/* publications.
 *
 * Ownership model:
 * - Each module defines its own static Route table (branch -> local message id).
 * - The module instantiates one producer with its own producerId.
 * - This helper only provides plumbing (event filtering, pending bitmap, default cfg JSON build).
 */
class MqttConfigRouteProducer {
public:
    static constexpr uint8_t MaxRoutes = 32;

    using CustomBuildFn = MqttBuildResult (*)(void* owner, uint16_t messageId, MqttBuildContext& ctx);

    /**
     * @brief Build "<base>" or "<base>/<suffix>" into dst.
     *
     * Purely mechanical helper with no MQTT/config business knowledge.
     */
    static bool buildRelativeTopic(char* dst,
                                   size_t dstCap,
                                   const char* base,
                                   const char* suffix,
                                   size_t& outLen);

    struct Route {
        uint16_t messageId = 0;
        ConfigBranchRef branch{};
        const char* moduleName = nullptr;   // ConfigStore module name used by toJsonModule()
        const char* topicSuffix = nullptr;  // relative suffix for topicBase, or legacy "<module>/<sub>"
        uint8_t changePriority = (uint8_t)MqttPublishPriority::Normal;
        CustomBuildFn customBuild = nullptr;
        const char* topicBase = nullptr;    // full logical MQTT suffix base (ex: "cfg/poollogic")
    };

    /**
     * @brief Configure and bind this producer against current services.
     *
     * Safe to call multiple times (init + onConfigLoaded).
     */
    void configure(void* owner,
                   uint8_t producerId,
                   const Route* routes,
                   uint8_t routeCount,
                   ServiceRegistry& services);

    void requestFullSync(MqttPublishPriority prio = MqttPublishPriority::Low);

private:
    void* owner_ = nullptr;
    uint8_t producerId_ = 0;
    const Route* routes_ = nullptr;
    uint8_t routeCount_ = 0;

    const MqttService* mqttSvc_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;

    bool producerRegistered_ = false;
    bool eventsSubscribed_ = false;
    bool configLoaded_ = false;
    bool mqttReadyLatched_ = false;
    uint32_t pendingMask_ = 0;
    uint32_t needsEnqueueMask_ = 0;
    uint32_t retryDueMs_ = 0;
    uint16_t retryBackoffMs_ = 0;
    uint8_t retryCursor_ = 0;
    uint32_t retryFirstRefusedMs_[MaxRoutes]{};
    uint32_t metricsWinStartMs_ = 0;
    uint32_t metricsRefusedWin_ = 0;
    uint32_t metricsRetryTryWin_ = 0;
    uint32_t metricsRetryOkWin_ = 0;
    uint32_t metricsTimeoutWin_ = 0;
    uint32_t metricsRefusedTotal_ = 0;
    uint32_t metricsRetryTryTotal_ = 0;
    uint32_t metricsRetryOkTotal_ = 0;
    uint32_t metricsTimeoutTotal_ = 0;

    MqttPublishProducer producer_{};

    int8_t findRouteByMessage_(uint16_t messageId) const;
    void setPending_(uint8_t idx, bool pending);
    bool isPending_(uint8_t idx) const;
    void setNeedsEnqueue_(uint8_t idx, bool needed);
    bool needsEnqueue_(uint8_t idx) const;
    bool hasNeedsEnqueue_() const;
    void armRetry_(uint32_t nowMs);
    void resetRetry_();
    void expireTimedOutRoutes_(uint32_t nowMs);
    uint8_t countPendingBits_(uint32_t mask) const;
    void reportMetrics_(uint32_t nowMs);
    void runRetryTick_(uint32_t nowMs);
    bool enqueueByRoute_(uint8_t idx, MqttPublishPriority prio);
    void refreshReadyGateAndMaybeSync_(bool triggerOnSteadyReady);
    static MqttPublishPriority routePriority_(const Route& route);

    void onEvent_(const Event& e);
    static void onEventStatic_(const Event& e, void* ctx);
    static void tickStatic_(void* ctx, uint32_t nowMs);
    void onTransportTick_(uint32_t nowMs);

    MqttBuildResult buildMessage_(uint16_t messageId, MqttBuildContext& ctx);
    void onMessagePublished_(uint16_t messageId);
    void onMessageDropped_(uint16_t messageId);

    static MqttBuildResult buildStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    static void publishedStatic_(void* ctx, uint16_t messageId);
    static void droppedStatic_(void* ctx, uint16_t messageId);
};
