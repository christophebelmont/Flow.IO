#pragma once

#include "Core/EventBus/EventPayloads.h"
#include "Core/ConfigBranchRef.h"
#include "Core/ServiceRegistry.h"
#include "Core/Services/Services.h"

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

    struct Route {
        uint16_t messageId = 0;
        ConfigBranchRef branch{};
        const char* moduleName = nullptr;   // ConfigStore module name used by toJsonModule()
        const char* topicSuffix = nullptr;  // cfg/<topicSuffix>; defaults to moduleName
        uint8_t changePriority = (uint8_t)MqttPublishPriority::Normal;
        CustomBuildFn customBuild = nullptr;
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
    uint32_t pendingMask_ = 0;

    MqttPublishProducer producer_{};

    int8_t findRouteByMessage_(uint16_t messageId) const;
    void setPending_(uint8_t idx, bool pending);
    bool isPending_(uint8_t idx) const;
    void enqueueByRoute_(uint8_t idx, MqttPublishPriority prio);

    void onEvent_(const Event& e);
    static void onEventStatic_(const Event& e, void* ctx);

    MqttBuildResult buildMessage_(uint16_t messageId, MqttBuildContext& ctx);
    void onMessagePublished_(uint16_t messageId);
    void onMessageDropped_(uint16_t messageId);

    static MqttBuildResult buildStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    static void publishedStatic_(void* ctx, uint16_t messageId);
    static void droppedStatic_(void* ctx, uint16_t messageId);
};
