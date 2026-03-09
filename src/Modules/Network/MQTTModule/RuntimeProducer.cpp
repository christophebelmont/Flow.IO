#include "Modules/Network/MQTTModule/RuntimeProducer.h"

#include <string.h>

void RuntimeProducer::configure(const MqttService* mqttSvc)
{
    mqttSvc_ = mqttSvc;
}

bool RuntimeProducer::registerProvider(const IRuntimeSnapshotProvider* provider)
{
    if (!provider) return false;
    if (providerCount_ >= MaxProviders) return false;

    for (uint8_t i = 0; i < providerCount_; ++i) {
        if (providers_[i] == provider) return true;
    }

    providers_[providerCount_++] = provider;
    rebuildRoutes();
    return true;
}

void RuntimeProducer::rebuildRoutes()
{
    routeCount_ = 0;

    for (uint8_t p = 0; p < providerCount_; ++p) {
        const IRuntimeSnapshotProvider* provider = providers_[p];
        if (!provider) continue;

        const uint8_t count = provider->runtimeSnapshotCount();
        for (uint8_t i = 0; i < count; ++i) {
            if (routeCount_ >= Limits::MaxRuntimeRoutes) return;

            const char* suffix = provider->runtimeSnapshotSuffix(i);
            if (!suffix || suffix[0] == '\0') continue;

            Route& route = routes_[routeCount_++];
            route = Route{};
            route.provider = provider;
            route.snapshotIdx = i;
            route.routeClass = provider->runtimeSnapshotClass(i);
            route.pending = true;
            route.force = true;
            snprintf(route.suffix, sizeof(route.suffix), "%s", suffix);
        }
    }
}

void RuntimeProducer::markRoutePending_(uint8_t idx, bool force)
{
    if (idx >= routeCount_) return;
    Route& route = routes_[idx];
    route.pending = true;
    route.force = route.force || force;
}

void RuntimeProducer::enqueueRoute_(uint8_t idx)
{
    if (!mqttSvc_ || !mqttSvc_->enqueue) return;
    if (idx >= routeCount_) return;

    const Route& route = routes_[idx];
    const MqttPublishPriority prio = (route.routeClass == RuntimeRouteClass::ActuatorImmediate)
        ? MqttPublishPriority::High
        : MqttPublishPriority::Normal;

    (void)mqttSvc_->enqueue(mqttSvc_->ctx, ProducerId, idx, (uint8_t)prio, 0);
}

void RuntimeProducer::onConnected()
{
    for (uint8_t i = 0; i < routeCount_; ++i) {
        markRoutePending_(i, true);
        enqueueRoute_(i);
    }
}

void RuntimeProducer::onDataChanged(DataKey key)
{
    for (uint8_t i = 0; i < routeCount_; ++i) {
        Route& route = routes_[i];
        if (!route.provider) continue;
        if (!route.provider->runtimeSnapshotAffectsKey(route.snapshotIdx, key)) continue;
        markRoutePending_(i, false);
        enqueueRoute_(i);
    }
}

MqttBuildResult RuntimeProducer::buildMessage(uint16_t messageId, MqttBuildContext& ctx)
{
    if (messageId >= routeCount_) return MqttBuildResult::NoLongerNeeded;

    Route& route = routes_[messageId];
    if (!route.provider) return MqttBuildResult::NoLongerNeeded;
    if (!route.pending && !route.force) return MqttBuildResult::NoLongerNeeded;

    const uint32_t nowMs = millis();
    if (!route.force && route.routeClass == RuntimeRouteClass::NumericThrottled) {
        if ((uint32_t)(nowMs - route.lastPublishMs) < NumericThrottleMs) {
            return MqttBuildResult::RetryLater;
        }
    }

    uint32_t ts = 0;
    if (!route.provider->buildRuntimeSnapshot(route.snapshotIdx, ctx.payload, ctx.payloadCapacity, ts)) {
        return MqttBuildResult::RetryLater;
    }

    const uint32_t effectiveTs = (ts == 0U) ? 1U : ts;
    if (!route.force && route.lastPublishedTs != 0U && effectiveTs <= route.lastPublishedTs) {
        route.pending = false;
        return MqttBuildResult::NoLongerNeeded;
    }

    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::PermanentError;
    mqttSvc_->formatTopic(mqttSvc_->ctx, route.suffix, ctx.topic, ctx.topicCapacity);
    if (ctx.topic[0] == '\0') return MqttBuildResult::PermanentError;

    ctx.topicLen = (uint16_t)strnlen(ctx.topic, ctx.topicCapacity);
    ctx.payloadLen = (uint16_t)strnlen(ctx.payload, ctx.payloadCapacity);
    ctx.qos = 0;
    ctx.retain = false;

    route.lastBuiltTs = effectiveTs;
    return MqttBuildResult::Ready;
}

void RuntimeProducer::onMessagePublished(uint16_t messageId)
{
    if (messageId >= routeCount_) return;

    Route& route = routes_[messageId];
    route.lastPublishedTs = route.lastBuiltTs;
    route.lastPublishMs = millis();
    route.pending = false;
    route.force = false;
}

void RuntimeProducer::onMessageDropped(uint16_t messageId)
{
    if (messageId >= routeCount_) return;
    routes_[messageId].pending = false;
}

