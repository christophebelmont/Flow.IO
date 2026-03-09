/**
 * @file EventBus.cpp
 * @brief Implementation file.
 */
#include "EventBus.h"
#include <Arduino.h>  // micros(), millis()
#include "Core/Log.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::CoreEventBus)

#if EVENTBUS_PROFILE
static uint32_t g_lastWarnMs = 0;
static bool canWarnNow() {
    uint32_t now = millis();
    if ((uint32_t)(now - g_lastWarnMs) < EVENTBUS_WARN_MIN_INTERVAL_MS) return false;
    g_lastWarnMs = now;
    return true;
}
#endif

EventBus::EventBus() {
    _queue = xQueueCreate(QUEUE_LENGTH, sizeof(QueuedEvent));
    _winStartMs = millis();
}

bool EventBus::subscribe(EventId id, EventCallback cb, void* user) {
    if (cb == nullptr) {
        recordSubscribeReject_(id, cb, user, SubRejectReason::NullCb);
        return false;
    }
    if (_count >= MAX_SUBSCRIBERS) {
        recordSubscribeReject_(id, cb, user, SubRejectReason::Capacity);
        return false;
    }

    _subs[_count].id = id;
    _subs[_count].cb = cb;
    _subs[_count].user = user;
    _count++;
    return true;
}

void EventBus::recordSubscribeReject_(EventId id, EventCallback cb, void* user, SubRejectReason reason)
{
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&_statsMux);
    const uint32_t seq = ++_subRejectTotal;
    if (reason == SubRejectReason::NullCb) ++_subRejectNullCbTotal;
    if (reason == SubRejectReason::Capacity) ++_subRejectCapacityTotal;

    SubscribeRejectInfo& slot = _subRejectRing[_subRejectWriteIdx];
    slot.seq = seq;
    slot.tsMs = nowMs;
    slot.eventId = (uint16_t)id;
    slot.cbAddr = (uintptr_t)cb;
    slot.userAddr = (uintptr_t)user;
    slot.reason = (uint8_t)reason;
    _subRejectWriteIdx = (uint8_t)((_subRejectWriteIdx + 1U) % SUB_REJECT_RING_CAP);
    portEXIT_CRITICAL(&_statsMux);
}

const char* EventBus::subRejectReasonStr_(uint8_t reason)
{
    if (reason == (uint8_t)SubRejectReason::NullCb) return "null_cb";
    if (reason == (uint8_t)SubRejectReason::Capacity) return "capacity";
    return "unknown";
}

bool EventBus::post(EventId id, const void* payload, size_t len) {
    if (_queue == nullptr) {
        portENTER_CRITICAL(&_statsMux);
        ++_postDropTotal;
        ++_postDropNoQueueTotal;
        ++_winDropCount;
        ++_winCurrentDropBurst;
        if (_winCurrentDropBurst > _winMaxDropBurst) _winMaxDropBurst = _winCurrentDropBurst;
        portEXIT_CRITICAL(&_statsMux);
        return false;
    }
    if (len > MAX_PAYLOAD_SIZE) {
        portENTER_CRITICAL(&_statsMux);
        ++_postDropTotal;
        ++_postDropTooLargeTotal;
        ++_winDropCount;
        ++_winCurrentDropBurst;
        if (_winCurrentDropBurst > _winMaxDropBurst) _winMaxDropBurst = _winCurrentDropBurst;
        portEXIT_CRITICAL(&_statsMux);
        return false;
    }

    QueuedEvent qe;
    qe.id = id;
    qe.len = static_cast<uint8_t>(len);
    if (len > 0 && payload != nullptr) {
        memcpy(qe.data, payload, len);
    }

    /// non-blocking send (0 ticks) to keep real-time constraints
    BaseType_t ok = xQueueSend(_queue, &qe, 0);
    portENTER_CRITICAL(&_statsMux);
    if (ok == pdTRUE) {
        ++_postOkTotal;
        _winCurrentDropBurst = 0;
    } else {
        ++_postDropTotal;
        ++_winDropCount;
        ++_winCurrentDropBurst;
        if (_winCurrentDropBurst > _winMaxDropBurst) _winMaxDropBurst = _winCurrentDropBurst;
    }
    portEXIT_CRITICAL(&_statsMux);
    return ok == pdTRUE;
}

bool EventBus::postFromISR(EventId id, const void* payload, size_t len) {
    if (_queue == nullptr) {
        portENTER_CRITICAL_ISR(&_statsMux);
        ++_postDropTotal;
        ++_postDropFromIsrTotal;
        ++_postDropNoQueueTotal;
        ++_winDropCount;
        ++_winCurrentDropBurst;
        if (_winCurrentDropBurst > _winMaxDropBurst) _winMaxDropBurst = _winCurrentDropBurst;
        portEXIT_CRITICAL_ISR(&_statsMux);
        return false;
    }
    if (len > MAX_PAYLOAD_SIZE) {
        portENTER_CRITICAL_ISR(&_statsMux);
        ++_postDropTotal;
        ++_postDropFromIsrTotal;
        ++_postDropTooLargeTotal;
        ++_winDropCount;
        ++_winCurrentDropBurst;
        if (_winCurrentDropBurst > _winMaxDropBurst) _winMaxDropBurst = _winCurrentDropBurst;
        portEXIT_CRITICAL_ISR(&_statsMux);
        return false;
    }

    QueuedEvent qe;
    qe.id = id;
    qe.len = static_cast<uint8_t>(len);
    if (len > 0 && payload != nullptr) {
        memcpy(qe.data, payload, len);
    }

    BaseType_t higherWoken = pdFALSE;
    BaseType_t ok = xQueueSendFromISR(_queue, &qe, &higherWoken);
    portENTER_CRITICAL_ISR(&_statsMux);
    if (ok == pdTRUE) {
        ++_postOkTotal;
        _winCurrentDropBurst = 0;
    } else {
        ++_postDropTotal;
        ++_postDropFromIsrTotal;
        ++_winDropCount;
        ++_winCurrentDropBurst;
        if (_winCurrentDropBurst > _winMaxDropBurst) _winMaxDropBurst = _winCurrentDropBurst;
    }
    portEXIT_CRITICAL_ISR(&_statsMux);
    if (higherWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    return ok == pdTRUE;
}

void EventBus::dispatch(uint16_t maxEvents) {
    if (_queue == nullptr) return;

    const uint32_t nowMs = millis();
    uint32_t winDropCount = 0;
    uint32_t winMaxDropBurst = 0;
    uint32_t totalOk = 0;
    uint32_t totalDrop = 0;
    uint32_t totalDropFromIsr = 0;
    uint32_t totalDropTooLarge = 0;
    uint32_t totalDropNoQueue = 0;
    uint32_t subRejectTotal = 0;
    uint32_t subRejectNullCb = 0;
    uint32_t subRejectCapacity = 0;
    uint32_t subRejectReportedSeq = 0;
    uint16_t subCount = 0;
    bool emitWindowLog = false;

    portENTER_CRITICAL(&_statsMux);
    if (_winStartMs == 0U) _winStartMs = nowMs;
    if ((uint32_t)(nowMs - _winStartMs) >= 5000U) {
        winDropCount = _winDropCount;
        winMaxDropBurst = _winMaxDropBurst;
        totalOk = _postOkTotal;
        totalDrop = _postDropTotal;
        totalDropFromIsr = _postDropFromIsrTotal;
        totalDropTooLarge = _postDropTooLargeTotal;
        totalDropNoQueue = _postDropNoQueueTotal;
        subRejectTotal = _subRejectTotal;
        subRejectNullCb = _subRejectNullCbTotal;
        subRejectCapacity = _subRejectCapacityTotal;
        subRejectReportedSeq = _subRejectReportedSeq;
        subCount = _count;
        if (subCount > MAX_SUBSCRIBERS) subCount = MAX_SUBSCRIBERS;
        _winStartMs = nowMs;
        _winDropCount = 0;
        _winCurrentDropBurst = 0;
        _winMaxDropBurst = 0;
        emitWindowLog = true;
    }
    portEXIT_CRITICAL(&_statsMux);

    if (emitWindowLog) {
        uint16_t subDataChanged = 0U;
        uint16_t subConfigChanged = 0U;
        uint16_t subSchedulerTriggered = 0U;
        uint16_t subAlarmEvents = 0U;
        uint16_t subOther = 0U;
        for (uint16_t i = 0; i < subCount; ++i) {
            const EventId id = _subs[i].id;
            if (id == EventId::DataChanged) {
                ++subDataChanged;
            } else if (id == EventId::ConfigChanged) {
                ++subConfigChanged;
            } else if (id == EventId::SchedulerEventTriggered) {
                ++subSchedulerTriggered;
            } else if (id == EventId::AlarmRaised ||
                       id == EventId::AlarmCleared ||
                       id == EventId::AlarmAcked ||
                       id == EventId::AlarmSilenceChanged ||
                       id == EventId::AlarmConditionChanged) {
                ++subAlarmEvents;
            } else {
                ++subOther;
            }
        }

        const bool postCritical = (winDropCount > 0U) ||
                                  (winMaxDropBurst > 0U) ||
                                  (totalDrop > 0U) ||
                                  (totalDropFromIsr > 0U) ||
                                  (totalDropTooLarge > 0U) ||
                                  (totalDropNoQueue > 0U);
        if (postCritical) {
            Log::warn(LOG_MODULE_ID,
                      "post stats 5s: drops=%lu max_burst=%lu ok_total=%lu drop_total=%lu isr=%lu too_large=%lu no_queue=%lu",
                      (unsigned long)winDropCount,
                      (unsigned long)winMaxDropBurst,
                      (unsigned long)totalOk,
                      (unsigned long)totalDrop,
                      (unsigned long)totalDropFromIsr,
                      (unsigned long)totalDropTooLarge,
                      (unsigned long)totalDropNoQueue);
        } else {
            Log::debug(LOG_MODULE_ID,
                       "post stats 5s: drops=%lu max_burst=%lu ok_total=%lu drop_total=%lu isr=%lu too_large=%lu no_queue=%lu",
                       (unsigned long)winDropCount,
                       (unsigned long)winMaxDropBurst,
                       (unsigned long)totalOk,
                       (unsigned long)totalDrop,
                       (unsigned long)totalDropFromIsr,
                       (unsigned long)totalDropTooLarge,
                       (unsigned long)totalDropNoQueue);
        }

        const UBaseType_t queuedNow = uxQueueMessagesWaiting(_queue);
        const bool subCritical = (subRejectTotal > 0U) ||
                                 (subRejectCapacity > 0U) ||
                                 (subRejectNullCb > 0U) ||
                                 (queuedNow >= QUEUE_LENGTH);
        if (subCritical) {
            Log::warn(LOG_MODULE_ID,
                      "sub stats 5s: used=%u/%u queue=%u/%u data=%u cfg=%u sched=%u alarm=%u other=%u rej_total=%lu cap=%lu null_cb=%lu",
                      (unsigned)subCount,
                      (unsigned)MAX_SUBSCRIBERS,
                      (unsigned)queuedNow,
                      (unsigned)QUEUE_LENGTH,
                      (unsigned)subDataChanged,
                      (unsigned)subConfigChanged,
                      (unsigned)subSchedulerTriggered,
                      (unsigned)subAlarmEvents,
                      (unsigned)subOther,
                      (unsigned long)subRejectTotal,
                      (unsigned long)subRejectCapacity,
                      (unsigned long)subRejectNullCb);
        } else {
            Log::debug(LOG_MODULE_ID,
                       "sub stats 5s: used=%u/%u queue=%u/%u data=%u cfg=%u sched=%u alarm=%u other=%u rej_total=%lu cap=%lu null_cb=%lu",
                       (unsigned)subCount,
                       (unsigned)MAX_SUBSCRIBERS,
                       (unsigned)queuedNow,
                       (unsigned)QUEUE_LENGTH,
                       (unsigned)subDataChanged,
                       (unsigned)subConfigChanged,
                       (unsigned)subSchedulerTriggered,
                       (unsigned)subAlarmEvents,
                       (unsigned)subOther,
                       (unsigned long)subRejectTotal,
                       (unsigned long)subRejectCapacity,
                       (unsigned long)subRejectNullCb);
        }

        if (subRejectTotal > subRejectReportedSeq) {
            uint32_t printedUpToSeq = subRejectReportedSeq;
            while (true) {
                uint32_t bestSeq = 0xFFFFFFFFUL;
                SubscribeRejectInfo best{};
                bool found = false;

                portENTER_CRITICAL(&_statsMux);
                for (uint8_t i = 0; i < SUB_REJECT_RING_CAP; ++i) {
                    const SubscribeRejectInfo& r = _subRejectRing[i];
                    if (r.seq == 0U) continue;
                    if (r.seq <= printedUpToSeq) continue;
                    if (!found || r.seq < bestSeq) {
                        bestSeq = r.seq;
                        best = r;
                        found = true;
                    }
                }
                portEXIT_CRITICAL(&_statsMux);
                if (!found) break;

                const uint32_t ageMs = (nowMs >= best.tsMs) ? (nowMs - best.tsMs) : 0U;
                Log::warn(LOG_MODULE_ID,
                          "sub reject seq=%lu event=%u reason=%s cb=0x%lx user=0x%lx age_ms=%lu",
                          (unsigned long)best.seq,
                          (unsigned)best.eventId,
                          subRejectReasonStr_(best.reason),
                          (unsigned long)best.cbAddr,
                          (unsigned long)best.userAddr,
                          (unsigned long)ageMs);

                printedUpToSeq = best.seq;
            }

            portENTER_CRITICAL(&_statsMux);
            if (_subRejectReportedSeq < printedUpToSeq) {
                _subRejectReportedSeq = printedUpToSeq;
            }
            portEXIT_CRITICAL(&_statsMux);
        }
    }

#if EVENTBUS_PROFILE
    const uint32_t tDispatch0 = micros();
    uint16_t dispatched = 0;
#endif

    for (uint16_t i = 0; i < maxEvents; i++) {
        QueuedEvent qe;
        BaseType_t ok = xQueueReceive(_queue, &qe, 0);
        if (ok != pdTRUE) {
            break;
        }

        dispatchOne(qe);

#if EVENTBUS_PROFILE
        dispatched++;
#endif
    }

#if EVENTBUS_PROFILE
    const uint32_t dt = (uint32_t)(micros() - tDispatch0);
    if (dispatched > 0 && dt > EVENTBUS_DISPATCH_WARN_US && canWarnNow()) {
        Log::warn(LOG_MODULE_ID,"dispatch slow: %u events dt=%lu us", (unsigned)dispatched, (unsigned long)dt);
    }
#endif
}

void EventBus::dispatchOne(const QueuedEvent& qe) {
    Event e;
    e.id = qe.id;
    e.payload = (qe.len > 0) ? qe.data : nullptr;
    e.len = qe.len;

    for (uint16_t i = 0; i < _count; i++) {
        if (_subs[i].id == qe.id && _subs[i].cb != nullptr) {

#if EVENTBUS_PROFILE
            const uint32_t t0 = micros();
#endif

            _subs[i].cb(e, _subs[i].user);

#if EVENTBUS_PROFILE
            const uint32_t dt = (uint32_t)(micros() - t0);
            if (dt > EVENTBUS_HANDLER_WARN_US && canWarnNow()) {
                Log::warn(LOG_MODULE_ID,"slow handler: event=%u cb=%p user=%p dt=%lu us",
                     (unsigned)qe.id,
                     (void*)_subs[i].cb,
                     _subs[i].user,
                     (unsigned long)dt);
            }
#endif
        }
    }
}
