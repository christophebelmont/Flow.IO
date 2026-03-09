#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"

#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MQTTModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr uint16_t kRetryMinMs = 200U;
static constexpr uint16_t kRetryMaxMs = 2000U;
static constexpr uint32_t kRetryTimeoutMs = 10000U;
}

bool MqttConfigRouteProducer::buildRelativeTopic(char* dst,
                                                 size_t dstCap,
                                                 const char* base,
                                                 const char* suffix,
                                                 size_t& outLen)
{
    outLen = 0U;
    if (!dst || dstCap == 0U || !base || base[0] == '\0') return false;

    const char* rel = (suffix) ? suffix : "";
    const size_t baseLen = strlen(base);
    const bool hasSuffix = (rel[0] != '\0');
    const bool needSep = (hasSuffix && baseLen > 0U && base[baseLen - 1U] != '/');

    int w = 0;
    if (!hasSuffix) {
        w = snprintf(dst, dstCap, "%s", base);
    } else if (needSep) {
        w = snprintf(dst, dstCap, "%s/%s", base, rel);
    } else {
        w = snprintf(dst, dstCap, "%s%s", base, rel);
    }
    if (!(w > 0 && (size_t)w < dstCap)) return false;
    outLen = (size_t)w;
    return true;
}

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
        producer_.onTransportTick = MqttConfigRouteProducer::tickStatic_;
        producerRegistered_ = mqttSvc_->registerProducer(mqttSvc_->ctx, &producer_);
    }

    // One side of the gate is now true: module config is loaded/configured.
    // If MQTT is already ready, this path triggers the initial full sync immediately.
    configLoaded_ = true;
    refreshReadyGateAndMaybeSync_(true);
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

void MqttConfigRouteProducer::setNeedsEnqueue_(uint8_t idx, bool needed)
{
    if (idx >= routeCount_ || idx >= 32U) return;
    const uint32_t mask = (1UL << idx);
    if (needed) needsEnqueueMask_ |= mask;
    else needsEnqueueMask_ &= ~mask;
}

bool MqttConfigRouteProducer::needsEnqueue_(uint8_t idx) const
{
    if (idx >= routeCount_ || idx >= 32U) return false;
    return (needsEnqueueMask_ & (1UL << idx)) != 0UL;
}

bool MqttConfigRouteProducer::hasNeedsEnqueue_() const
{
    return needsEnqueueMask_ != 0UL;
}

void MqttConfigRouteProducer::armRetry_(uint32_t nowMs)
{
    if (retryBackoffMs_ < kRetryMinMs) retryBackoffMs_ = kRetryMinMs;
    retryDueMs_ = nowMs + retryBackoffMs_;
    if (retryBackoffMs_ < kRetryMaxMs) {
        uint32_t next = (uint32_t)retryBackoffMs_ * 2U;
        if (next > kRetryMaxMs) next = kRetryMaxMs;
        retryBackoffMs_ = (uint16_t)next;
    }
}

void MqttConfigRouteProducer::resetRetry_()
{
    retryDueMs_ = 0U;
    retryBackoffMs_ = 0U;
}

void MqttConfigRouteProducer::expireTimedOutRoutes_(uint32_t nowMs)
{
    for (uint8_t i = 0; i < routeCount_ && i < MaxRoutes; ++i) {
        if (!needsEnqueue_(i)) continue;
        const uint32_t firstRefusedMs = retryFirstRefusedMs_[i];
        if (firstRefusedMs == 0U) continue;
        if ((uint32_t)(nowMs - firstRefusedMs) < kRetryTimeoutMs) continue;

        // Timeout reached: this logical cfg publication is considered lost.
        setNeedsEnqueue_(i, false);
        setPending_(i, false);
        retryFirstRefusedMs_[i] = 0U;
        ++metricsTimeoutWin_;
        ++metricsTimeoutTotal_;
    }
}

uint8_t MqttConfigRouteProducer::countPendingBits_(uint32_t mask) const
{
    uint8_t c = 0U;
    for (uint8_t i = 0; i < routeCount_ && i < 32U; ++i) {
        if (mask & (1UL << i)) ++c;
    }
    return c;
}

void MqttConfigRouteProducer::reportMetrics_(uint32_t nowMs)
{
    if (metricsWinStartMs_ == 0U) {
        metricsWinStartMs_ = nowMs;
        return;
    }
    if ((uint32_t)(nowMs - metricsWinStartMs_) < 5000U) return;
    metricsWinStartMs_ = nowMs;

    const uint8_t pendingEnq = countPendingBits_(needsEnqueueMask_);
    const uint8_t pendingAny = countPendingBits_(pendingMask_);
    if (metricsRefusedWin_ == 0U && metricsRetryTryWin_ == 0U &&
        metricsRetryOkWin_ == 0U && metricsTimeoutWin_ == 0U &&
        pendingEnq == 0U && pendingAny == 0U) {
        return;
    }

    const bool cfgCritical = (metricsTimeoutTotal_ > 0U);
    if (cfgCritical) {
        LOGW("cfgq p=%u e=%u rf=%lu rt=%lu ok=%lu to=%lu tf=%lu tt=%lu tk=%lu tto=%lu",
             (unsigned)pendingAny,
             (unsigned)pendingEnq,
             (unsigned long)metricsRefusedWin_,
             (unsigned long)metricsRetryTryWin_,
             (unsigned long)metricsRetryOkWin_,
             (unsigned long)metricsTimeoutWin_,
             (unsigned long)metricsRefusedTotal_,
             (unsigned long)metricsRetryTryTotal_,
             (unsigned long)metricsRetryOkTotal_,
             (unsigned long)metricsTimeoutTotal_);
    } else {
        LOGD("cfgq p=%u e=%u rf=%lu rt=%lu ok=%lu to=%lu tf=%lu tt=%lu tk=%lu tto=%lu",
             (unsigned)pendingAny,
             (unsigned)pendingEnq,
             (unsigned long)metricsRefusedWin_,
             (unsigned long)metricsRetryTryWin_,
             (unsigned long)metricsRetryOkWin_,
             (unsigned long)metricsTimeoutWin_,
             (unsigned long)metricsRefusedTotal_,
             (unsigned long)metricsRetryTryTotal_,
             (unsigned long)metricsRetryOkTotal_,
             (unsigned long)metricsTimeoutTotal_);
    }

    metricsRefusedWin_ = 0U;
    metricsRetryTryWin_ = 0U;
    metricsRetryOkWin_ = 0U;
    metricsTimeoutWin_ = 0U;
}

MqttPublishPriority MqttConfigRouteProducer::routePriority_(const Route& route)
{
    if (route.changePriority == (uint8_t)MqttPublishPriority::Low) return MqttPublishPriority::Low;
    if (route.changePriority == (uint8_t)MqttPublishPriority::High) return MqttPublishPriority::High;
    return MqttPublishPriority::Normal;
}

void MqttConfigRouteProducer::runRetryTick_(uint32_t nowMs)
{
    if (!producerRegistered_ || !configLoaded_) return;
    if (!dsSvc_ || !dsSvc_->store) return;
    if (!mqttReady(*dsSvc_->store)) return;
    expireTimedOutRoutes_(nowMs);
    if (!hasNeedsEnqueue_()) {
        resetRetry_();
        return;
    }
    if (retryDueMs_ != 0U && (int32_t)(nowMs - retryDueMs_) < 0) return;

    for (uint8_t n = 0; n < routeCount_; ++n) {
        const uint8_t idx = (uint8_t)((retryCursor_ + n) % routeCount_);
        if (!needsEnqueue_(idx)) continue;
        const MqttPublishPriority prio = routePriority_(routes_[idx]);
        retryCursor_ = (uint8_t)((idx + 1U) % routeCount_);
        ++metricsRetryTryWin_;
        ++metricsRetryTryTotal_;
        const bool accepted = enqueueByRoute_(idx, prio);
        if (accepted) {
            ++metricsRetryOkWin_;
            ++metricsRetryOkTotal_;
        }
        return;
    }

    resetRetry_();
}

bool MqttConfigRouteProducer::enqueueByRoute_(uint8_t idx, MqttPublishPriority prio)
{
    if (!mqttSvc_ || !mqttSvc_->enqueue) return false;
    if (idx >= routeCount_) return false;
    setPending_(idx, true);
    setNeedsEnqueue_(idx, true);
    constexpr uint8_t kFlags = (uint8_t)MqttEnqueueFlags::SilentRejectLog;
    const bool accepted = mqttSvc_->enqueue(mqttSvc_->ctx, producerId_, routes_[idx].messageId, (uint8_t)prio, kFlags);
    const uint32_t nowMs = millis();
    if (accepted) {
        setNeedsEnqueue_(idx, false);
        retryFirstRefusedMs_[idx] = 0U;
        if (!hasNeedsEnqueue_()) {
            resetRetry_();
        } else if (retryDueMs_ == 0U || (int32_t)(nowMs - retryDueMs_) >= 0) {
            retryDueMs_ = nowMs + kRetryMinMs;
        }
        return true;
    }

    ++metricsRefusedWin_;
    ++metricsRefusedTotal_;
    if (idx < MaxRoutes && retryFirstRefusedMs_[idx] == 0U) {
        retryFirstRefusedMs_[idx] = nowMs;
    }
    armRetry_(nowMs);
    return false;
}

void MqttConfigRouteProducer::refreshReadyGateAndMaybeSync_(bool triggerOnSteadyReady)
{
    if (!dsSvc_ || !dsSvc_->store) {
        mqttReadyLatched_ = false;
        return;
    }

    const bool readyNow = mqttReady(*dsSvc_->store);
    const bool rising = readyNow && !mqttReadyLatched_;
    mqttReadyLatched_ = readyNow;
    if (!readyNow) return;
    if (!producerRegistered_ || !configLoaded_) return;
    if (rising || triggerOnSteadyReady) {
        requestFullSync(MqttPublishPriority::Low);
    }
}

void MqttConfigRouteProducer::requestFullSync(MqttPublishPriority prio)
{
    for (uint8_t i = 0; i < routeCount_; ++i) {
        (void)enqueueByRoute_(i, prio);
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
        // Other side of the gate: MQTT ready changed. Sync only when both
        // configLoaded and mqttReady are validated.
        refreshReadyGateAndMaybeSync_(false);
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
            const bool moduleMatch =
                (routes_[i].branch.moduleId == ConfigBranchRef::UnknownModule) ||
                (routes_[i].branch.moduleId == p->moduleId);
            if (!moduleMatch) continue;

            const bool localMatch =
                (routes_[i].branch.localBranchId == ConfigBranchRef::UnknownLocalBranch) ||
                (routes_[i].branch.localBranchId == p->localBranchId);
            if (!localMatch) continue;

            const MqttPublishPriority prio = routePriority_(routes_[i]);
            (void)enqueueByRoute_(i, prio);
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

    char relativeTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    size_t relativeLen = 0U;
    if (route.topicBase && route.topicBase[0] != '\0') {
        const char* relSuffix = route.topicSuffix ? route.topicSuffix : "";
        if (!buildRelativeTopic(relativeTopic,
                                sizeof(relativeTopic),
                                route.topicBase,
                                relSuffix,
                                relativeLen)) {
            return MqttBuildResult::PermanentError;
        }
    } else {
        const char* topicSuffix = route.topicSuffix ? route.topicSuffix : route.moduleName;
        if (!topicSuffix || topicSuffix[0] == '\0') return MqttBuildResult::PermanentError;
        const int sw = snprintf(relativeTopic, sizeof(relativeTopic), "cfg/%s", topicSuffix);
        if (!(sw > 0 && (size_t)sw < sizeof(relativeTopic))) return MqttBuildResult::PermanentError;
        relativeLen = (size_t)sw;
    }

    (void)relativeLen;
    mqttSvc_->formatTopic(mqttSvc_->ctx, relativeTopic, ctx.topic, ctx.topicCapacity);
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
    const uint8_t idx = (uint8_t)routeIdx;
    setPending_(idx, false);
    setNeedsEnqueue_(idx, false);
    retryFirstRefusedMs_[idx] = 0U;
    if (!hasNeedsEnqueue_()) {
        resetRetry_();
    }
}

void MqttConfigRouteProducer::onMessageDropped_(uint16_t messageId)
{
    const int8_t routeIdx = findRouteByMessage_(messageId);
    if (routeIdx < 0) return;
    const uint8_t idx = (uint8_t)routeIdx;
    setPending_(idx, false);
    setNeedsEnqueue_(idx, false);
    retryFirstRefusedMs_[idx] = 0U;
    if (!hasNeedsEnqueue_()) {
        resetRetry_();
    }
}

void MqttConfigRouteProducer::tickStatic_(void* ctx, uint32_t nowMs)
{
    MqttConfigRouteProducer* self = static_cast<MqttConfigRouteProducer*>(ctx);
    if (self) self->onTransportTick_(nowMs);
}

void MqttConfigRouteProducer::onTransportTick_(uint32_t nowMs)
{
    runRetryTick_(nowMs);
    reportMetrics_(nowMs);
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
