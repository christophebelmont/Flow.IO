/**
 * @file WebInterfaceLifecycle.cpp
 * @brief Event handling and task loop for WebInterfaceModule.
 */

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WebInterfaceModule)
#include "WebInterfaceModule.h"

#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/ModuleLog.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"
#include <esp_heap_caps.h>

bool WebInterfaceModule::setPaused_(bool paused)
{
    uartPaused_ = paused;
    if (paused) {
        lineLen_ = 0;
    }
    return true;
}

bool WebInterfaceModule::isPaused_() const
{
    return uartPaused_;
}

void WebInterfaceModule::onEventStatic_(const Event& e, void* user)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(user);
    if (!self) return;
    self->onEvent_(e);
}

void WebInterfaceModule::onEvent_(const Event& e)
{
    if (e.id != EventId::DataChanged) return;
    if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
    const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
    if (p->id != DataKeys::WifiReady) return;

    netReady_ = dataStore_ ? wifiReady(*dataStore_) : false;
}

void WebInterfaceModule::loop()
{
    if (!netAccessSvc_ && services_) {
        netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
    }

    if (!started_) {
        char ip[16] = {0};
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!getNetworkIp_(ip, sizeof(ip), &mode) || ip[0] == '\0' || mode == NetworkAccessMode::None) {
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }

        const bool bootNetworkReady = (mode == NetworkAccessMode::AccessPoint) ? true : netReady_;
        if (!bootNetworkReady) {
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }

        const char* modeText = (mode == NetworkAccessMode::AccessPoint) ? "ap" : "station";
        LOGI("Web startup release mode=%s ip=%s starting server", modeText, ip);
        const uint32_t minHeapBeforeStart = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        startServer_();
        const uint32_t minHeapAfterStart = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        const long minHeapDelta = (long)minHeapAfterStart - (long)minHeapBeforeStart;
        LOGI("Web heap min around startup: before=%lu after=%lu delta=%ld",
             (unsigned long)minHeapBeforeStart,
             (unsigned long)minHeapAfterStart,
             minHeapDelta);
    }

    if (uartPaused_) {
        flushLocalLogQueue_();
        if (started_) {
            ws_.cleanupClients();
            wsLog_.cleanupClients();
        }
        vTaskDelay(pdMS_TO_TICKS(40));
        return;
    }

    while (uart_.available() > 0) {
        int raw = uart_.read();
        if (raw < 0) break;

        const uint8_t c = static_cast<uint8_t>(raw);

        if (c == '\r') continue;
        if (c == '\n') {
            flushLine_(true);
            continue;
        }

        if (lineLen_ >= (kLineBufferSize - 1)) {
            flushLine_(true);
        }

        if (lineLen_ < (kLineBufferSize - 1)) {
            lineBuf_[lineLen_++] = isLogByte_(c) ? static_cast<char>(c) : '.';
        }
    }

    flushLocalLogQueue_();

    if (started_) ws_.cleanupClients();
    if (started_) wsLog_.cleanupClients();

    vTaskDelay(pdMS_TO_TICKS(10));
}
