#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"

#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <stdio.h>
#include <string.h>

void MqttConfigRouteProducer::configure(void* owner,
                                        uint8_t producerId,
                                        const Route* routes,
                                        uint8_t routeCount,
                                        ServiceRegistry& services)
{
    owner_ = owner;
    producerId_ = producerId;
    routes_ = routes;
    routeCount_ = (routeCount > MaxRoutes) ? MaxRoutes : routeCount;

    mqttSvc_ = services.get<MqttService>("mqtt");
    cfgSvc_ = services.get<ConfigStoreService>("config");
    dsSvc_ = services.get<DataStoreService>("datastore");
    const EventBusService* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    if (!eventsSubscribed_ && eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &MqttConfigRouteProducer::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &MqttConfigRouteProducer::onEventStatic_, this);
        eventsSubscribed_ = true;
    }

    if (!producerRegistered_ && mqttSvc_ && mqttSvc_->registerProducer && routeCount_ > 0U) {
        producer_ = MqttPublishProducer{};
        producer_.producerId = producerId_;
        producer_.ctx = this;
        producer_.buildMessage = MqttConfigRouteProducer::buildStatic_;
        producer_.onMessagePublished = MqttConfigRouteProducer::publishedStatic_;
        producer_.onMessageDropped = MqttConfigRouteProducer::droppedStatic_;
        producerRegistered_ = mqttSvc_->registerProducer(mqttSvc_->ctx, &producer_);
    }
}

int8_t MqttConfigRouteProducer::findRouteByMessage_(uint16_t messageId) const
{
    for (uint8_t i = 0; i < routeCount_; ++i) {
        if (routes_[i].messageId == messageId) return (int8_t)i;
    }
    return -1;
}

void MqttConfigRouteProducer::setPending_(uint8_t idx, bool pending)
{
    if (idx >= routeCount_ || idx >= 32U) return;
    const uint32_t mask = (1UL << idx);
    if (pending) pendingMask_ |= mask;
    else pendingMask_ &= ~mask;
}

bool MqttConfigRouteProducer::isPending_(uint8_t idx) const
{
    if (idx >= routeCount_ || idx >= 32U) return false;
    return (pendingMask_ & (1UL << idx)) != 0UL;
}

void MqttConfigRouteProducer::enqueueByRoute_(uint8_t idx, MqttPublishPriority prio)
{
    if (!mqttSvc_ || !mqttSvc_->enqueue) return;
    if (idx >= routeCount_) return;
    setPending_(idx, true);
    (void)mqttSvc_->enqueue(mqttSvc_->ctx, producerId_, routes_[idx].messageId, (uint8_t)prio, 0);
}

void MqttConfigRouteProducer::requestFullSync(MqttPublishPriority prio)
{
    for (uint8_t i = 0; i < routeCount_; ++i) {
        enqueueByRoute_(i, prio);
    }
}

void MqttConfigRouteProducer::onEventStatic_(const Event& e, void* ctx)
{
    MqttConfigRouteProducer* self = static_cast<MqttConfigRouteProducer*>(ctx);
    if (self) self->onEvent_(e);
}

void MqttConfigRouteProducer::onEvent_(const Event& e)
{
    if (e.id == EventId::DataChanged) {
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (!p) return;
        if (p->id != DATAKEY_MQTT_READY) return;

        if (!dsSvc_ || !dsSvc_->store) {
            requestFullSync(MqttPublishPriority::Low);
            return;
        }
        if (mqttReady(*dsSvc_->store)) {
            requestFullSync(MqttPublishPriority::Low);
        }
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (!p) return;

        if (p->moduleId == ConfigBranchRef::UnknownModule ||
            p->localBranchId == ConfigBranchRef::UnknownLocalBranch) {
            requestFullSync(MqttPublishPriority::Low);
            return;
        }

        for (uint8_t i = 0; i < routeCount_; ++i) {
            if (routes_[i].branch.moduleId != p->moduleId ||
                routes_[i].branch.localBranchId != p->localBranchId) {
                continue;
            }
            MqttPublishPriority prio = MqttPublishPriority::Normal;
            if (routes_[i].changePriority == (uint8_t)MqttPublishPriority::Low) prio = MqttPublishPriority::Low;
            else if (routes_[i].changePriority == (uint8_t)MqttPublishPriority::High) prio = MqttPublishPriority::High;
            enqueueByRoute_(i, prio);
        }
    }
}

MqttBuildResult MqttConfigRouteProducer::buildMessage_(uint16_t messageId, MqttBuildContext& ctx)
{
    const int8_t routeIdx = findRouteByMessage_(messageId);
    if (routeIdx < 0) return MqttBuildResult::NoLongerNeeded;

    const uint8_t idx = (uint8_t)routeIdx;
    if (!isPending_(idx)) return MqttBuildResult::NoLongerNeeded;
    const Route& route = routes_[idx];

    if (route.customBuild) {
        return route.customBuild(owner_, messageId, ctx);
    }

    if (!mqttSvc_ || !mqttSvc_->formatTopic || !cfgSvc_ || !cfgSvc_->toJsonModule) {
        return MqttBuildResult::RetryLater;
    }
    if (!route.moduleName || route.moduleName[0] == '\0') return MqttBuildResult::PermanentError;

    const char* topicSuffix = route.topicSuffix ? route.topicSuffix : route.moduleName;
    if (!topicSuffix || topicSuffix[0] == '\0') return MqttBuildResult::PermanentError;

    char suffix[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    const int sw = snprintf(suffix, sizeof(suffix), "cfg/%s", topicSuffix);
    if (!(sw > 0 && (size_t)sw < sizeof(suffix))) return MqttBuildResult::PermanentError;

    mqttSvc_->formatTopic(mqttSvc_->ctx, suffix, ctx.topic, ctx.topicCapacity);
    if (ctx.topic[0] == '\0') return MqttBuildResult::PermanentError;

    bool truncated = false;
    const bool any = cfgSvc_->toJsonModule(cfgSvc_->ctx, route.moduleName, ctx.payload, ctx.payloadCapacity, &truncated);
    if (truncated) {
        if (!writeErrorJson(ctx.payload, ctx.payloadCapacity, ErrorCode::CfgTruncated, "cfg")) {
            snprintf(ctx.payload, ctx.payloadCapacity, "{\"ok\":false}");
        }
    } else if (!any) {
        return MqttBuildResult::NoLongerNeeded;
    }

    ctx.topicLen = (uint16_t)strnlen(ctx.topic, ctx.topicCapacity);
    ctx.payloadLen = (uint16_t)strnlen(ctx.payload, ctx.payloadCapacity);
    ctx.qos = 1;
    ctx.retain = true;
    return MqttBuildResult::Ready;
}

void MqttConfigRouteProducer::onMessagePublished_(uint16_t messageId)
{
    const int8_t routeIdx = findRouteByMessage_(messageId);
    if (routeIdx < 0) return;
    setPending_((uint8_t)routeIdx, false);
}

void MqttConfigRouteProducer::onMessageDropped_(uint16_t messageId)
{
    const int8_t routeIdx = findRouteByMessage_(messageId);
    if (routeIdx < 0) return;
    setPending_((uint8_t)routeIdx, false);
}

MqttBuildResult MqttConfigRouteProducer::buildStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    MqttConfigRouteProducer* self = static_cast<MqttConfigRouteProducer*>(ctx);
    return self ? self->buildMessage_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

void MqttConfigRouteProducer::publishedStatic_(void* ctx, uint16_t messageId)
{
    MqttConfigRouteProducer* self = static_cast<MqttConfigRouteProducer*>(ctx);
    if (self) self->onMessagePublished_(messageId);
}

void MqttConfigRouteProducer::droppedStatic_(void* ctx, uint16_t messageId)
{
    MqttConfigRouteProducer* self = static_cast<MqttConfigRouteProducer*>(ctx);
    if (self) self->onMessageDropped_(messageId);
}
