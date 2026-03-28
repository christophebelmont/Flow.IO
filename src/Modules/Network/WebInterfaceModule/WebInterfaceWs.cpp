/**
 * @file WebInterfaceWs.cpp
 * @brief WebSocket transport and UART-to-web flow control for WebInterfaceModule.
 */

#include "WebInterfaceModule.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WebInterfaceModule)
#include "Core/ModuleLog.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#ifndef FLOW_WEB_HEAP_FORENSICS
#define FLOW_WEB_HEAP_FORENSICS 0
#endif

namespace {
struct HeapForensicSnapshot {
    uint32_t freeBytes = 0;
    uint32_t minFreeBytes = 0;
    uint32_t largestFreeBlock = 0;
};

HeapForensicSnapshot captureHeapForensicSnapshot_()
{
    HeapForensicSnapshot snap{};
    snap.freeBytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snap.minFreeBytes = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snap.largestFreeBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    return snap;
}

const char* wsEventName_(AwsEventType type)
{
    switch (type) {
    case WS_EVT_CONNECT: return "connect";
    case WS_EVT_DISCONNECT: return "disconnect";
    case WS_EVT_DATA: return "data";
    case WS_EVT_PONG: return "pong";
    case WS_EVT_ERROR: return "error";
    default: return "other";
    }
}

const char* wsSendStatusName_(AsyncWebSocket::SendStatus status)
{
    switch (status) {
    case AsyncWebSocket::DISCARDED: return "drop";
    case AsyncWebSocket::ENQUEUED: return "q";
    case AsyncWebSocket::PARTIALLY_ENQUEUED: return "part";
    default: return "other";
    }
}

#if FLOW_WEB_HEAP_FORENSICS
void logWsEventHeapForensic_(const char* channel,
                             AwsEventType type,
                             AsyncWebSocketClient* client,
                             size_t len,
                             uint32_t clients,
                             uint32_t startUs,
                             const HeapForensicSnapshot& startHeap)
{
    const HeapForensicSnapshot endHeap = captureHeapForensicSnapshot_();
    const uint32_t elapsedUs = micros() - startUs;
    const long deltaFree = (long)endHeap.freeBytes - (long)startHeap.freeBytes;
    const uint32_t lowWaterDrop =
        (startHeap.minFreeBytes > endHeap.minFreeBytes) ? (startHeap.minFreeBytes - endHeap.minFreeBytes) : 0U;
    LOGW("WSfx %s %s id=%lu len=%u n=%u us=%lu f0=%lu f1=%lu df=%ld m1=%lu lo=%lu",
         channel ? channel : "?",
         wsEventName_(type),
         (unsigned long)(client ? client->id() : 0U),
         (unsigned)len,
         (unsigned)clients,
         (unsigned long)elapsedUs,
         (unsigned long)startHeap.freeBytes,
         (unsigned long)endHeap.freeBytes,
         deltaFree,
         (unsigned long)endHeap.minFreeBytes,
         (unsigned long)lowWaterDrop);
}

void logWsSendHeapForensic_(const char* channel,
                            const char* result,
                            size_t len,
                            uint32_t clients,
                            uint32_t startUs,
                            const HeapForensicSnapshot& startHeap)
{
    const HeapForensicSnapshot endHeap = captureHeapForensicSnapshot_();
    const uint32_t elapsedUs = micros() - startUs;
    const long deltaFree = (long)endHeap.freeBytes - (long)startHeap.freeBytes;
    const uint32_t lowWaterDrop =
        (startHeap.minFreeBytes > endHeap.minFreeBytes) ? (startHeap.minFreeBytes - endHeap.minFreeBytes) : 0U;
    LOGW("WSfx %s tx r=%s len=%u n=%u us=%lu f0=%lu f1=%lu df=%ld m1=%lu lo=%lu",
         channel ? channel : "?",
         result ? result : "?",
         (unsigned)len,
         (unsigned)clients,
         (unsigned long)elapsedUs,
         (unsigned long)startHeap.freeBytes,
         (unsigned long)endHeap.freeBytes,
         deltaFree,
         (unsigned long)endHeap.minFreeBytes,
         (unsigned long)lowWaterDrop);
}
#endif
} // namespace

void WebInterfaceModule::onWsEvent_(AsyncWebSocket*,
                                    AsyncWebSocketClient* client,
                                    AwsEventType type,
                                    void* arg,
                                    uint8_t* data,
                                    size_t len)
{
#if FLOW_WEB_HEAP_FORENSICS
    const uint32_t forensicStartUs = micros();
    const HeapForensicSnapshot forensicStartHeap = captureHeapForensicSnapshot_();
    const auto logForensic = [&](uint32_t clients) {
        logWsEventHeapForensic_("wsserial", type, client, len, clients, forensicStartUs, forensicStartHeap);
    };
#endif

    if (type == WS_EVT_CONNECT) {
        ++wsFlowConnectCount_;
        if (client) {
            client->setCloseClientOnQueueFull(false);
            client->keepAlivePeriod(15);
            client->text("[webinterface] connecté");
            LOGI("wsserial connect id=%lu clients=%u connects=%lu",
                 (unsigned long)client->id(),
                 (unsigned)ws_.count(),
                 (unsigned long)wsFlowConnectCount_);
        }
#if FLOW_WEB_HEAP_FORENSICS
        logForensic((uint32_t)ws_.count());
#endif
        return;
    }

    if (type == WS_EVT_DISCONNECT) {
        ++wsFlowDisconnectCount_;
        LOGW("wsserial disconnect id=%lu clients=%u disconnects=%lu sent=%lu dropped=%lu partial=%lu discarded=%lu heap=%lu",
             (unsigned long)(client ? client->id() : 0U),
             (unsigned)ws_.count(),
             (unsigned long)wsFlowDisconnectCount_,
             (unsigned long)wsFlowSentCount_,
             (unsigned long)wsFlowDropCount_,
             (unsigned long)wsFlowPartialCount_,
             (unsigned long)wsFlowDiscardCount_,
             (unsigned long)ESP.getFreeHeap());
#if FLOW_WEB_HEAP_FORENSICS
        logForensic((uint32_t)ws_.count());
#endif
        return;
    }

    if (type != WS_EVT_DATA || !arg || !data || len == 0) {
#if FLOW_WEB_HEAP_FORENSICS
        if (type == WS_EVT_ERROR) {
            logForensic((uint32_t)ws_.count());
        }
#endif
        return;
    }

    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) {
#if FLOW_WEB_HEAP_FORENSICS
        logForensic((uint32_t)ws_.count());
#endif
        return;
    }

    constexpr size_t kMaxIncoming = 192;
    char msg[kMaxIncoming] = {0};
    size_t n = (len < (kMaxIncoming - 1)) ? len : (kMaxIncoming - 1);
    memcpy(msg, data, n);
    msg[n] = '\0';

    if (uartPaused_) {
        if (client) client->text("[webinterface] uart occupé (mise à jour firmware en cours)");
#if FLOW_WEB_HEAP_FORENSICS
        logForensic((uint32_t)ws_.count());
#endif
        return;
    }

    uart_.write(reinterpret_cast<const uint8_t*>(msg), n);
    uart_.write('\n');
#if FLOW_WEB_HEAP_FORENSICS
    logForensic((uint32_t)ws_.count());
#endif
}

void WebInterfaceModule::onWsLogEvent_(AsyncWebSocket*,
                                       AsyncWebSocketClient* client,
                                       AwsEventType type,
                                       void*,
                                       uint8_t*,
                                       size_t)
{
#if FLOW_WEB_HEAP_FORENSICS
    const uint32_t forensicStartUs = micros();
    const HeapForensicSnapshot forensicStartHeap = captureHeapForensicSnapshot_();
#endif

    if (type == WS_EVT_CONNECT) {
        if (client) {
            client->setCloseClientOnQueueFull(true);
            client->keepAlivePeriod(15);
            client->text("[webinterface] logs supervisor connectes");
        }
    }

#if FLOW_WEB_HEAP_FORENSICS
    if (type == WS_EVT_CONNECT || type == WS_EVT_DISCONNECT || type == WS_EVT_ERROR) {
        logWsEventHeapForensic_("wslog",
                                type,
                                client,
                                0U,
                                (uint32_t)wsLog_.count(),
                                forensicStartUs,
                                forensicStartHeap);
    }
#endif
}

void WebInterfaceModule::flushLine_(bool force)
{
    if (lineLen_ == 0) return;
    if (!force) return;

    const size_t payloadLen = lineLen_;
    lineBuf_[lineLen_] = '\0';
    if (ws_.count() == 0) {
        lineLen_ = 0;
        return;
    }

#if FLOW_WEB_HEAP_FORENSICS
    const uint32_t forensicStartUs = micros();
    const HeapForensicSnapshot forensicStartHeap = captureHeapForensicSnapshot_();
#endif

    if (!ws_.availableForWriteAll()) {
        ++wsFlowDropCount_;
        logWsFlowPressure_("queue_full");
#if FLOW_WEB_HEAP_FORENSICS
        logWsSendHeapForensic_("wsserial",
                               "qfull",
                               payloadLen,
                               (uint32_t)ws_.count(),
                               forensicStartUs,
                               forensicStartHeap);
#endif
        lineLen_ = 0;
        return;
    }

    const AsyncWebSocket::SendStatus status = ws_.textAll(lineBuf_);
    if (status == AsyncWebSocket::ENQUEUED) {
        ++wsFlowSentCount_;
    } else {
        ++wsFlowDropCount_;
        if (status == AsyncWebSocket::PARTIALLY_ENQUEUED) {
            ++wsFlowPartialCount_;
            logWsFlowPressure_("partial_enqueue");
        } else {
            ++wsFlowDiscardCount_;
            logWsFlowPressure_("discarded");
        }
    }
#if FLOW_WEB_HEAP_FORENSICS
    logWsSendHeapForensic_("wsserial",
                           wsSendStatusName_(status),
                           payloadLen,
                           (uint32_t)ws_.count(),
                           forensicStartUs,
                           forensicStartHeap);
#endif
    lineLen_ = 0;
}

void WebInterfaceModule::logWsFlowPressure_(const char* reason)
{
    const uint32_t nowMs = millis();
    if ((nowMs - wsFlowLastPressureLogMs_) < 2000U) return;
    wsFlowLastPressureLogMs_ = nowMs;
    LOGW("wsserial pressure reason=%s clients=%u sent=%lu dropped=%lu partial=%lu discarded=%lu heap=%lu",
         reason ? reason : "unknown",
         (unsigned)ws_.count(),
         (unsigned long)wsFlowSentCount_,
         (unsigned long)wsFlowDropCount_,
         (unsigned long)wsFlowPartialCount_,
         (unsigned long)wsFlowDiscardCount_,
         (unsigned long)ESP.getFreeHeap());
}
