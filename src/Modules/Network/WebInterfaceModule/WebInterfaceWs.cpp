/**
 * @file WebInterfaceWs.cpp
 * @brief WebSocket transport and UART-to-web flow control for WebInterfaceModule.
 */

#include "WebInterfaceModule.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WebInterfaceModule)
#include "Core/ModuleLog.h"

#include <Arduino.h>
#include <string.h>

void WebInterfaceModule::onWsEvent_(AsyncWebSocket*,
                                    AsyncWebSocketClient* client,
                                    AwsEventType type,
                                    void* arg,
                                    uint8_t* data,
                                    size_t len)
{
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
        return;
    }

    if (type != WS_EVT_DATA || !arg || !data || len == 0) return;

    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

    constexpr size_t kMaxIncoming = 192;
    char msg[kMaxIncoming] = {0};
    size_t n = (len < (kMaxIncoming - 1)) ? len : (kMaxIncoming - 1);
    memcpy(msg, data, n);
    msg[n] = '\0';

    if (uartPaused_) {
        if (client) client->text("[webinterface] uart occupé (mise à jour firmware en cours)");
        return;
    }

    uart_.write(reinterpret_cast<const uint8_t*>(msg), n);
    uart_.write('\n');
}

void WebInterfaceModule::onWsLogEvent_(AsyncWebSocket*,
                                       AsyncWebSocketClient* client,
                                       AwsEventType type,
                                       void*,
                                       uint8_t*,
                                       size_t)
{
    if (type == WS_EVT_CONNECT) {
        if (client) {
            client->setCloseClientOnQueueFull(true);
            client->keepAlivePeriod(15);
            client->text("[webinterface] logs supervisor connectes");
        }
    }
}

void WebInterfaceModule::flushLine_(bool force)
{
    if (lineLen_ == 0) return;
    if (!force) return;

    lineBuf_[lineLen_] = '\0';
    if (ws_.count() == 0) {
        lineLen_ = 0;
        return;
    }

    if (!ws_.availableForWriteAll()) {
        ++wsFlowDropCount_;
        logWsFlowPressure_("queue_full");
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
