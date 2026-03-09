#pragma once
/**
 * @file EventBus.h
 * @brief Simple queued event bus with fixed-size payloads.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "EventId.h"
#include "Core/SystemLimits.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifndef EVENTBUS_PROFILE
#define EVENTBUS_PROFILE 1
#endif

#ifndef EVENTBUS_HANDLER_WARN_US
#define EVENTBUS_HANDLER_WARN_US 5000  // 5ms
#endif

#ifndef EVENTBUS_DISPATCH_WARN_US
#define EVENTBUS_DISPATCH_WARN_US 20000 // 20ms for a batch
#endif

#ifndef EVENTBUS_WARN_MIN_INTERVAL_MS
#define EVENTBUS_WARN_MIN_INTERVAL_MS 2000
#endif

/** @brief Event delivered to subscribers during dispatch(). */
struct Event {
    EventId id;
    const void* payload;
    size_t len;
};

/** @brief Callback signature for event subscribers. */
using EventCallback = void(*)(const Event& e, void* user);

/**
 * @brief Thread-safe event queue with subscriber dispatch.
 */
class EventBus {
public:
    static constexpr uint16_t MAX_SUBSCRIBERS = Limits::EventSubscribersMax;
    static constexpr uint8_t SUB_REJECT_RING_CAP = 8;

    // Maximum payload size copied into internal queue.
    static constexpr uint8_t MAX_PAYLOAD_SIZE = 48;

    // Maximum number of queued events (FreeRTOS queue length).
    static constexpr uint8_t QUEUE_LENGTH = Limits::EventQueueLen;

    /** @brief Construct and initialize the event queue. */
    EventBus();
    ~EventBus() = default;

    /** @brief Subscribe to an event id (not thread-safe; call during init). */
    bool subscribe(EventId id, EventCallback cb, void* user);

    /**
     * @brief Post an event from any task; payload is copied into the queue.
     */
    bool post(EventId id, const void* payload = nullptr, size_t len = 0);

    /** @brief Post an event from ISR context. */
    bool postFromISR(EventId id, const void* payload = nullptr, size_t len = 0);

    /** @brief Dispatch queued events and call subscribers. */
    void dispatch(uint16_t maxEvents = 8);

private:
    enum class SubRejectReason : uint8_t {
        None = 0,
        NullCb = 1,
        Capacity = 2,
    };

    struct Subscriber {
        EventId id;
        EventCallback cb;
        void* user;
    };

    struct SubscribeRejectInfo {
        uint32_t seq = 0;
        uint32_t tsMs = 0;
        uint16_t eventId = 0;
        uintptr_t cbAddr = 0;
        uintptr_t userAddr = 0;
        uint8_t reason = (uint8_t)SubRejectReason::None;
    };

    struct QueuedEvent {
        EventId id;
        uint8_t len;
        uint8_t data[MAX_PAYLOAD_SIZE];
    };

    Subscriber _subs[MAX_SUBSCRIBERS];
    uint16_t _count = 0;

    QueueHandle_t _queue = nullptr;
    portMUX_TYPE _statsMux = portMUX_INITIALIZER_UNLOCKED;
    uint32_t _postOkTotal = 0;
    uint32_t _postDropTotal = 0;
    uint32_t _postDropFromIsrTotal = 0;
    uint32_t _postDropTooLargeTotal = 0;
    uint32_t _postDropNoQueueTotal = 0;
    uint32_t _winStartMs = 0;
    uint32_t _winDropCount = 0;
    uint32_t _winCurrentDropBurst = 0;
    uint32_t _winMaxDropBurst = 0;
    uint32_t _subRejectTotal = 0;
    uint32_t _subRejectNullCbTotal = 0;
    uint32_t _subRejectCapacityTotal = 0;
    uint32_t _subRejectReportedSeq = 0;
    SubscribeRejectInfo _subRejectRing[SUB_REJECT_RING_CAP]{};
    uint8_t _subRejectWriteIdx = 0;

    void dispatchOne(const QueuedEvent& qe);
    void recordSubscribeReject_(EventId id, EventCallback cb, void* user, SubRejectReason reason);
    static const char* subRejectReasonStr_(uint8_t reason);
};
