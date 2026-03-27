/**
 * @file WebInterfaceServer.cpp
 * @brief HTTP server wiring and network-facing endpoints for WebInterfaceModule.
 */

#include "WebInterfaceModule.h"

#include "Board/BoardSpec.h"
#include "Core/FirmwareVersion.h"
#include "Core/Generated/RuntimeUiManifest_Generated.h"
#include "Core/I2cCfgProtocol.h"
#include "Core/SystemLimits.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::WebInterfaceModule)
#include "Core/ModuleLog.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

#ifndef FLOW_WEB_HIDE_MENU_SVG
#define FLOW_WEB_HIDE_MENU_SVG 0
#endif

#ifndef FLOW_WEB_UNIFY_STATUS_CARD_ICONS
#define FLOW_WEB_UNIFY_STATUS_CARD_ICONS 0
#endif

static void sanitizeJsonString_(char* s)
{
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') {
            s[i] = ' ';
        }
    }
}

static void printJsonEscaped_(Print& out, const char* s)
{
    out.print('\"');
    if (s) {
        for (const char* p = s; *p != '\0'; ++p) {
            switch (*p) {
            case '\"': out.print("\\\""); break;
            case '\\': out.print("\\\\"); break;
            case '\b': out.print("\\b"); break;
            case '\f': out.print("\\f"); break;
            case '\n': out.print("\\n"); break;
            case '\r': out.print("\\r"); break;
            case '\t': out.print("\\t"); break;
            default:
                if ((uint8_t)*p < 0x20U) {
                    out.print('?');
                } else {
                    out.print(*p);
                }
                break;
            }
        }
    }
    out.print('\"');
}

static bool parseBoolParam_(const String& in, bool fallback)
{
    if (in.length() == 0) return fallback;
    if (in.equalsIgnoreCase("1") || in.equalsIgnoreCase("true") || in.equalsIgnoreCase("yes") ||
        in.equalsIgnoreCase("on")) {
        return true;
    }
    if (in.equalsIgnoreCase("0") || in.equalsIgnoreCase("false") || in.equalsIgnoreCase("no") ||
        in.equalsIgnoreCase("off")) {
        return false;
    }
    return fallback;
}

template <size_t N>
static inline void sendProgmemLiteral_(AsyncWebServerRequest* request, const char* contentType, const char (&content)[N])
{
    if (!request || !contentType || N == 0U) return;
    request->send(200, contentType, reinterpret_cast<const uint8_t*>(content), N - 1U);
}

namespace {
constexpr uint32_t kHttpLatencyInfoMs = 40U;
constexpr uint32_t kHttpLatencyWarnMs = 120U;
constexpr uint32_t kHttpLatencyFlowCfgInfoMs = 200U;
constexpr uint32_t kHttpLatencyFlowCfgWarnMs = 900U;

const char* webAssetVersion_()
{
    return FirmwareVersion::BuildRef;
}

void addNoCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
}

void addShortLivedAssetCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "public, max-age=3600");
}

void addVersionedAssetCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
}

void addCacheAwareAssetHeaders_(AsyncWebServerRequest* request, AsyncWebServerResponse* response)
{
    if (!request || !response) return;
    if (request->hasParam("v")) {
        addVersionedAssetCacheHeaders_(response);
    } else {
        addShortLivedAssetCacheHeaders_(response);
    }
}

int flowCfgApplyHttpStatus_(const char* ackJson)
{
    if (!ackJson || ackJson[0] == '\0') return 500;
    if (strstr(ackJson, "\"code\":\"BadCfgJson\"")) return 400;
    if (strstr(ackJson, "\"code\":\"ArgsTooLarge\"") || strstr(ackJson, "\"code\":\"CfgTruncated\"")) return 413;
    if (strstr(ackJson, "\"code\":\"NotReady\"")) return 503;
    if (strstr(ackJson, "\"code\":\"CfgApplyFailed\"")) return 409;
    if (strstr(ackJson, "\"code\":\"IoError\"")) return 502;
    if (strstr(ackJson, "\"code\":\"Failed\"")) return 502;
    return 500;
}

bool parseFlowStatusDomainParam_(const String& raw, FlowStatusDomain& domainOut)
{
    if (raw.equalsIgnoreCase("system")) {
        domainOut = FlowStatusDomain::System;
        return true;
    }
    if (raw.equalsIgnoreCase("wifi")) {
        domainOut = FlowStatusDomain::Wifi;
        return true;
    }
    if (raw.equalsIgnoreCase("mqtt")) {
        domainOut = FlowStatusDomain::Mqtt;
        return true;
    }
    if (raw.equalsIgnoreCase("i2c")) {
        domainOut = FlowStatusDomain::I2c;
        return true;
    }
    if (raw.equalsIgnoreCase("pool")) {
        domainOut = FlowStatusDomain::Pool;
        return true;
    }
    return false;
}

const char* httpMethodName_(uint8_t method)
{
    switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "OTHER";
    }
}

const char* runtimeUiWireTypeName_(RuntimeUiWireType type)
{
    switch (type) {
    case RuntimeUiWireType::NotFound: return "not_found";
    case RuntimeUiWireType::Unavailable: return "unavailable";
    case RuntimeUiWireType::Bool: return "bool";
    case RuntimeUiWireType::Int32: return "int32";
    case RuntimeUiWireType::UInt32: return "uint32";
    case RuntimeUiWireType::Float32: return "float";
    case RuntimeUiWireType::Enum: return "enum";
    case RuntimeUiWireType::String: return "string";
    default: return "unknown";
    }
}

size_t runtimeUiWireEstimate_(const RuntimeUiManifestItem* item)
{
    if (!item || !item->type) return 20U;
    if (strcmp(item->type, "bool") == 0) return 4U;
    if (strcmp(item->type, "enum") == 0) return 4U;
    if (strcmp(item->type, "int32") == 0) return 7U;
    if (strcmp(item->type, "uint32") == 0) return 7U;
    if (strcmp(item->type, "float") == 0) return 7U;
    if (strcmp(item->type, "string") == 0) {
        if (strcmp(item->key, "mqtt.server") == 0) return 72U;
        return 24U;
    }
    return 20U;
}

uint32_t readLe32_(const uint8_t* in)
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

bool appendRuntimeUiJsonValues_(JsonArray values, const uint8_t* payload, size_t payloadLen)
{
    if (!payload || payloadLen == 0U) return true;

    size_t offset = 0U;
    const uint8_t count = payload[offset++];
    for (uint8_t i = 0; i < count; ++i) {
        if ((offset + 3U) > payloadLen) return false;
        const RuntimeUiId runtimeId = (RuntimeUiId)((RuntimeUiId)payload[offset] |
                                                    ((RuntimeUiId)payload[offset + 1U] << 8));
        offset += 2U;
        const RuntimeUiWireType wireType = (RuntimeUiWireType)payload[offset++];
        const RuntimeUiManifestItem* manifestItem = findRuntimeUiManifestItem(runtimeId);

        JsonObject value = values.createNestedObject();
        value["id"] = runtimeId;
        if (manifestItem) {
            value["key"] = manifestItem->key;
            value["type"] = manifestItem->type;
            if (manifestItem->unit && manifestItem->unit[0] != '\0') {
                value["unit"] = manifestItem->unit;
            }
        } else {
            value["type"] = runtimeUiWireTypeName_(wireType);
        }

        switch (wireType) {
        case RuntimeUiWireType::NotFound:
            value["status"] = "not_found";
            break;

        case RuntimeUiWireType::Unavailable:
            value["status"] = "unavailable";
            break;

        case RuntimeUiWireType::Bool:
            if ((offset + 1U) > payloadLen) return false;
            value["value"] = (payload[offset++] != 0U);
            break;

        case RuntimeUiWireType::Int32: {
            if ((offset + 4U) > payloadLen) return false;
            int32_t raw = 0;
            const uint32_t bits = readLe32_(payload + offset);
            memcpy(&raw, &bits, sizeof(raw));
            value["value"] = raw;
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::UInt32:
            if ((offset + 4U) > payloadLen) return false;
            value["value"] = readLe32_(payload + offset);
            offset += 4U;
            break;

        case RuntimeUiWireType::Float32: {
            if ((offset + 4U) > payloadLen) return false;
            const uint32_t bits = readLe32_(payload + offset);
            float raw = 0.0f;
            memcpy(&raw, &bits, sizeof(raw));
            value["value"] = raw;
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::Enum:
            if ((offset + 1U) > payloadLen) return false;
            value["value"] = payload[offset++];
            break;

        case RuntimeUiWireType::String: {
            if ((offset + 1U) > payloadLen) return false;
            const uint8_t len = payload[offset++];
            if ((offset + len) > payloadLen) return false;
            char text[I2cCfgProtocol::MaxPayload + 1U] = {0};
            memcpy(text, payload + offset, len);
            text[len] = '\0';
            value["value"] = text;
            offset += len;
            break;
        }

        default:
            return false;
        }
    }

    return offset == payloadLen;
}

bool appendRuntimeUiJsonValuesToStream_(Print& out, const uint8_t* payload, size_t payloadLen, bool& firstValue)
{
    if (!payload || payloadLen == 0U) return true;

    size_t offset = 0U;
    const uint8_t count = payload[offset++];
    for (uint8_t i = 0; i < count; ++i) {
        if ((offset + 3U) > payloadLen) return false;
        const RuntimeUiId runtimeId = (RuntimeUiId)((RuntimeUiId)payload[offset] |
                                                    ((RuntimeUiId)payload[offset + 1U] << 8));
        offset += 2U;
        const RuntimeUiWireType wireType = (RuntimeUiWireType)payload[offset++];
        const RuntimeUiManifestItem* manifestItem = findRuntimeUiManifestItem(runtimeId);

        if (!firstValue) out.print(',');
        firstValue = false;

        out.print('{');
        out.print("\"id\":");
        out.print((unsigned)runtimeId);
        out.print(",\"key\":");
        printJsonEscaped_(out, manifestItem ? manifestItem->key : "");
        out.print(",\"type\":");
        printJsonEscaped_(out, manifestItem ? manifestItem->type : runtimeUiWireTypeName_(wireType));
        if (manifestItem && manifestItem->unit && manifestItem->unit[0] != '\0') {
            out.print(",\"unit\":");
            printJsonEscaped_(out, manifestItem->unit);
        }

        switch (wireType) {
        case RuntimeUiWireType::NotFound:
            out.print(",\"status\":\"not_found\"}");
            break;

        case RuntimeUiWireType::Unavailable:
            out.print(",\"status\":\"unavailable\"}");
            break;

        case RuntimeUiWireType::Bool:
            if ((offset + 1U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((payload[offset++] != 0U) ? "true" : "false");
            out.print('}');
            break;

        case RuntimeUiWireType::Int32: {
            if ((offset + 4U) > payloadLen) return false;
            int32_t raw = 0;
            const uint32_t bits = readLe32_(payload + offset);
            memcpy(&raw, &bits, sizeof(raw));
            out.print(",\"value\":");
            out.print((int32_t)raw);
            out.print('}');
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::UInt32:
            if ((offset + 4U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((unsigned long)readLe32_(payload + offset));
            out.print('}');
            offset += 4U;
            break;

        case RuntimeUiWireType::Float32: {
            if ((offset + 4U) > payloadLen) return false;
            const uint32_t bits = readLe32_(payload + offset);
            float raw = 0.0f;
            memcpy(&raw, &bits, sizeof(raw));
            out.print(",\"value\":");
            out.print(raw, 3);
            out.print('}');
            offset += 4U;
            break;
        }

        case RuntimeUiWireType::Enum:
            if ((offset + 1U) > payloadLen) return false;
            out.print(",\"value\":");
            out.print((unsigned)payload[offset++]);
            out.print('}');
            break;

        case RuntimeUiWireType::String: {
            if ((offset + 1U) > payloadLen) return false;
            const uint8_t len = payload[offset++];
            if ((offset + len) > payloadLen) return false;
            char text[I2cCfgProtocol::MaxPayload + 1U] = {0};
            memcpy(text, payload + offset, len);
            text[len] = '\0';
            out.print(",\"value\":");
            printJsonEscaped_(out, text);
            out.print('}');
            offset += len;
            break;
        }

        default:
            return false;
        }
    }

    return offset == payloadLen;
}

struct HttpLatencyScope {
    AsyncWebServerRequest* req;
    const char* route;
    uint32_t startUs;
    uint32_t infoMs;
    uint32_t warnMs;

    HttpLatencyScope(AsyncWebServerRequest* request,
                     const char* routePath,
                     uint32_t infoThresholdMs = kHttpLatencyInfoMs,
                     uint32_t warnThresholdMs = kHttpLatencyWarnMs)
        : req(request),
          route(routePath),
          startUs(micros()),
          infoMs(infoThresholdMs),
          warnMs((warnThresholdMs > infoThresholdMs) ? warnThresholdMs : (infoThresholdMs + 1U)) {}

    ~HttpLatencyScope()
    {
        const uint32_t elapsedUs = micros() - startUs;
        const uint32_t elapsedMs = elapsedUs / 1000U;
        if (elapsedMs < infoMs) return;

        const char* method = req ? httpMethodName_(req->method()) : "?";
        const uint32_t heapFree = (uint32_t)ESP.getFreeHeap();
        const uint32_t heapLargest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if (elapsedMs >= warnMs) {
            LOGW("HTTP slow %s %s latency=%lums heap=%lu largest=%lu",
                 method,
                 route ? route : "?",
                 (unsigned long)elapsedMs,
                 (unsigned long)heapFree,
                 (unsigned long)heapLargest);
        } else {
            LOGI("HTTP %s %s latency=%lums heap=%lu largest=%lu",
                 method,
                 route ? route : "?",
                 (unsigned long)elapsedMs,
                 (unsigned long)heapFree,
                 (unsigned long)heapLargest);
        }
    }
};

const UartSpec& webBridgeUartSpec_(const BoardSpec& board)
{
    static constexpr UartSpec kFallback{"bridge", 2, 16, 17, 115200, false};
    const UartSpec* spec = boardFindUart(board, "bridge");
    return spec ? *spec : kFallback;
}
} // namespace

static const char kWebInterfaceFallbackPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head><meta charset="utf-8" /><meta name="viewport" content="width=device-width, initial-scale=1" /><title>Superviseur Flow.IO</title></head>
<body style="font-family:Arial,sans-serif;background:#0B1F3A;color:#FFFFFF;padding:16px;">
<h1>Superviseur Flow.IO</h1>
<p>Interface web indisponible (fichiers SPIFFS manquants).</p>
<p>Veuillez charger SPIFFS puis recharger cette page.</p>
</body></html>
)HTML";

WebInterfaceModule::WebInterfaceModule(const BoardSpec& board)
{
    const UartSpec& uart = webBridgeUartSpec_(board);
    uartBaud_ = uart.baud;
    uartRxPin_ = uart.rxPin;
    uartTxPin_ = uart.txPin;
}

void WebInterfaceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;

    services_ = &services;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    logSinkReg_ = services.get<LogSinkRegistryService>(ServiceId::LogSinks);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    flowCfgSvc_ = services.get<FlowCfgRemoteService>(ServiceId::FlowCfg);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    auto* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    fwUpdateSvc_ = services.get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &WebInterfaceModule::onEventStatic_, this);
    }

    if (!services.add(ServiceId::WebInterface, &webInterfaceSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::WebInterface));
    }

    uart_.setRxBufferSize(kUartRxBufferSize);
    uart_.begin(uartBaud_, SERIAL_8N1, uartRxPin_, uartTxPin_);
    netReady_ = dataStore_ ? wifiReady(*dataStore_) : false;

    if (!localLogQueue_) {
        localLogQueue_ = xQueueCreate(kLocalLogQueueLen, kLocalLogLineMax);
        if (!localLogQueue_) {
            LOGW("WebInterface local log queue alloc failed");
        }
    }
    if (!localLogSinkRegistered_ && localLogQueue_ && logSinkReg_ && logSinkReg_->add) {
        const LogSinkService sink{&WebInterfaceModule::onLocalLogSinkWrite_, this};
        if (logSinkReg_->add(logSinkReg_->ctx, sink)) {
            localLogSinkRegistered_ = true;
        } else {
            LOGW("WebInterface local log sink registration failed");
        }
    }

    LOGI("WebInterface init uart=Serial2 baud=%lu rx=%d tx=%d line_buf=%u rx_buf=%u (server deferred)",
         (unsigned long)uartBaud_,
         uartRxPin_,
         uartTxPin_,
         (unsigned)kLineBufferSize,
         (unsigned)kUartRxBufferSize);
}

void WebInterfaceModule::startServer_()
{
    if (started_) return;

    spiffsReady_ = SPIFFS.begin(false);
    if (!spiffsReady_) {
        LOGW("SPIFFS mount failed; web assets unavailable");
    } else {
        LOGI("SPIFFS mounted for web assets");
    }

    auto beginSpiffsAssetResponse =
        [this](AsyncWebServerRequest* request,
               const char* assetPath,
               const char* contentType,
               bool cacheAware,
               const char* gzipOverridePath = nullptr) -> AsyncWebServerResponse* {
        if (!request || !assetPath || !contentType || !spiffsReady_) return nullptr;

        const size_t assetPathLen = strlen(assetPath);
        if (assetPathLen == 0U || assetPathLen >= 112U) return nullptr;

        char gzipPath[128] = {0};
        const char* servedPath = assetPath;
        bool hasGzip = false;
        if (gzipOverridePath && gzipOverridePath[0] != '\0') {
            if (SPIFFS.exists(gzipOverridePath)) {
                servedPath = gzipOverridePath;
                hasGzip = true;
            }
        } else {
            const int gzipPathLen = snprintf(gzipPath, sizeof(gzipPath), "%s.gz", assetPath);
            if ((gzipPathLen > 0) && ((size_t)gzipPathLen < sizeof(gzipPath)) && SPIFFS.exists(gzipPath)) {
                servedPath = gzipPath;
                hasGzip = true;
            }
        }
        if (!SPIFFS.exists(servedPath)) return nullptr;

        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, servedPath, contentType);
        if (!response) return nullptr;
        response->addHeader("Vary", "Accept-Encoding");
        if (hasGzip) {
            response->addHeader("Content-Encoding", "gzip");
        }
        if (cacheAware) {
            addCacheAwareAssetHeaders_(request, response);
        } else {
            addNoCacheHeaders_(response);
        }
        return response;
    };

    server_.on("/assets/favicon.png", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!spiffsReady_ || !SPIFFS.exists("/assets/Logos_Favicon.png")) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        request->send(SPIFFS, "/assets/Logos_Favicon.png", "image/png");
    });
    server_.on("/assets/flowio-logo-v2.png", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!spiffsReady_ || !SPIFFS.exists("/assets/Logos_Texte_v2.png")) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        request->send(SPIFFS, "/assets/Logos_Texte_v2.png", "image/png");
    });
    auto webInterfaceLandingUrl = [this]() -> String {
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }
        return (mode == NetworkAccessMode::AccessPoint)
            ? String("/webinterface?page=page-system")
            : String("/webinterface");
    };

    server_.on("/", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    server_.on("/webinterface/app.css", HTTP_GET, [this, beginSpiffsAssetResponse](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/app.css", "text/css", true);
        if (!response) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        request->send(response);
    });
    server_.on("/webinterface/app.js", HTTP_GET, [this, beginSpiffsAssetResponse](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/app.js", "application/javascript", true);
        if (!response) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        request->send(response);
    });
    server_.on("/webinterface/runtimeui.json", HTTP_GET, [this, beginSpiffsAssetResponse](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/runtimeui.json", "application/json", true);
        if (!response) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        request->send(response);
    });
    auto registerWebSvgRoute = [this, beginSpiffsAssetResponse](const char* assetPath) {
        server_.on(assetPath, HTTP_GET, [this, beginSpiffsAssetResponse, assetPath](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* response =
                beginSpiffsAssetResponse(request, assetPath, "image/svg+xml", true);
            if (!response) {
                request->send(404, "text/plain", "Not found");
                return;
            }
            request->send(response);
        });
    };
    registerWebSvgRoute("/webinterface/i/m.svg");
    registerWebSvgRoute("/webinterface/i/t.svg");
    registerWebSvgRoute("/webinterface/i/s.svg");
    registerWebSvgRoute("/webinterface/i/d.svg");
    registerWebSvgRoute("/webinterface/i/e.svg");
    registerWebSvgRoute("/webinterface/i/f.svg");
    registerWebSvgRoute("/webinterface/i/u.svg");
    server_.on("/webinterface/cfgdocs.fr.json", HTTP_GET, [this, beginSpiffsAssetResponse](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/cfgdocs.fr.json", "application/json", true, "/webinterface/cfgdocs.jz");
        if (response) {
            request->send(response);
            return;
        }
        AsyncWebServerResponse* fallbackResponse =
            request->beginResponse(200, "application/json", "{\"_meta\":{\"generated\":false},\"docs\":{}}");
        addNoCacheHeaders_(fallbackResponse);
        request->send(fallbackResponse);
    });
    server_.on("/webinterface/cfgmods.fr.json", HTTP_GET, [this, beginSpiffsAssetResponse](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(
                request, "/webinterface/cfgmods.fr.json", "application/json", true, "/webinterface/cfgmods.jz");
        if (response) {
            request->send(response);
            return;
        }
        AsyncWebServerResponse* fallbackResponse =
            request->beginResponse(200, "application/json", "{\"_meta\":{\"generated\":false},\"docs\":{}}");
        addNoCacheHeaders_(fallbackResponse);
        request->send(fallbackResponse);
    });
    server_.on("/api/web/meta", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/web/meta");
        StaticJsonDocument<320> doc;
        doc["ok"] = true;
        doc["web_asset_version"] = webAssetVersion_();
        doc["firmware_version"] = FirmwareVersion::Full;
        doc["hide_menu_svg"] = (FLOW_WEB_HIDE_MENU_SVG != 0);
        doc["unify_status_card_icons"] = (FLOW_WEB_UNIFY_STATUS_CARD_ICONS != 0);
        doc["upms"] = (uint32_t)millis();
        JsonObject heap = doc.createNestedObject("heap");
        heap["free"] = (uint32_t)ESP.getFreeHeap();
        heap["min_free"] = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

        char out[320] = {0};
        const size_t n = serializeJson(doc, out, sizeof(out));
        if (n == 0 || n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"web.meta\"}}");
            return;
        }

        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", out);
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface", HTTP_GET, [this, beginSpiffsAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/webinterface");
        if (!request->hasParam("page")) {
            NetworkAccessMode mode = NetworkAccessMode::None;
            if (!netAccessSvc_ && services_) {
                netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
            }
            if (netAccessSvc_ && netAccessSvc_->mode) {
                mode = netAccessSvc_->mode(netAccessSvc_->ctx);
            } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
                mode = NetworkAccessMode::Station;
            }
            if (mode == NetworkAccessMode::AccessPoint) {
                request->redirect("/webinterface?page=page-system");
                return;
            }
        }
        if (spiffsReady_ && SPIFFS.exists("/webinterface/index.html")) {
            AsyncWebServerResponse* response =
                beginSpiffsAssetResponse(request, "/webinterface/index.html", "text/html", false);
            if (!response) {
                request->send(500, "text/plain", "Failed to load web interface");
                return;
            }
            request->send(response);
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html", kWebInterfaceFallbackPage);
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface/", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/webserial", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    server_.on("/webinterface/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "ok");
    });
    server_.on("/webserial/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface/health");
    });
    server_.on("/api/network/mode", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/network/mode");
        NetworkAccessMode mode = NetworkAccessMode::None;
        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->mode) {
            mode = netAccessSvc_->mode(netAccessSvc_->ctx);
        } else if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
            mode = NetworkAccessMode::Station;
        }

        const char* modeTxt = "none";
        if (mode == NetworkAccessMode::Station) modeTxt = "station";
        else if (mode == NetworkAccessMode::AccessPoint) modeTxt = "ap";

        char ip[16] = {0};
        (void)getNetworkIp_(ip, sizeof(ip), nullptr);

        char out[96] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"mode\":\"%s\",\"ip\":\"%s\"}",
                               modeTxt,
                               ip);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"network.mode\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });
    server_.on("/generate_204", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/gen_204", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/hotspot-detect.html", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/connecttest.txt", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });
    server_.on("/ncsi.txt", HTTP_GET, [webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    auto fwStatusHandler = [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/status");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->statusJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.status\"}}");
            return;
        }

        char out[320] = {0};
        if (!fwUpdateSvc_->statusJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.status\"}}");
            return;
        }
        request->send(200, "application/json", out);
    };
    server_.on("/fwupdate/status", HTTP_GET, fwStatusHandler);
    server_.on("/api/fwupdate/status", HTTP_GET, fwStatusHandler);

    server_.on("/api/fwupdate/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/config");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->configJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.config\"}}");
            return;
        }

        char out[512] = {0};
        if (!fwUpdateSvc_->configJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.config\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/fwupdate/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/fwupdate/config");
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->setConfig) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        String hostStr;
        String flowStr;
        String supStr;
        String nxStr;
        String cfgdocsStr;
        if (request->hasParam("update_host", true)) {
            hostStr = request->getParam("update_host", true)->value();
        }
        if (request->hasParam("flowio_path", true)) {
            flowStr = request->getParam("flowio_path", true)->value();
        }
        if (request->hasParam("supervisor_path", true)) {
            supStr = request->getParam("supervisor_path", true)->value();
        }
        if (request->hasParam("nextion_path", true)) {
            nxStr = request->getParam("nextion_path", true)->value();
        }
        if (request->hasParam("cfgdocs_path", true)) {
            cfgdocsStr = request->getParam("cfgdocs_path", true)->value();
        }

        char err[96] = {0};
        if (!fwUpdateSvc_->setConfig(fwUpdateSvc_->ctx,
                                     hostStr.c_str(),
                                     flowStr.c_str(),
                                     supStr.c_str(),
                                     nxStr.c_str(),
                                     cfgdocsStr.c_str(),
                                     err,
                                     sizeof(err))) {
            request->send(409,
                          "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/wifi/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        char wifiJson[320] = {0};
        if (!cfgStore_->toJsonModule("wifi", wifiJson, sizeof(wifiJson), nullptr, false)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        StaticJsonDocument<320> doc;
        const DeserializationError err = deserializeJson(doc, wifiJson);
        if (err || !doc.is<JsonObjectConst>()) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidData\",\"where\":\"wifi.config.get\"}}");
            return;
        }

        JsonObjectConst root = doc.as<JsonObjectConst>();
        bool enabled = root["enabled"] | true;
        const char* ssid = root["ssid"] | "";
        const char* pass = root["pass"] | "";

        char ssidSafe[96] = {0};
        char passSafe[96] = {0};
        snprintf(ssidSafe, sizeof(ssidSafe), "%s", ssid ? ssid : "");
        snprintf(passSafe, sizeof(passSafe), "%s", pass ? pass : "");
        sanitizeJsonString_(ssidSafe);
        sanitizeJsonString_(passSafe);

        char out[360] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"enabled\":%s,\"ssid\":\"%s\",\"pass\":\"%s\"}",
                               enabled ? "true" : "false",
                               ssidSafe,
                               passSafe);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.get\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/wifi/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/config");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        const String enabledStr = request->hasParam("enabled", true)
                                      ? request->getParam("enabled", true)->value()
                                      : String("1");
        const bool enabled = parseBoolParam_(enabledStr, true);
        const String ssid = request->hasParam("ssid", true)
                                ? request->getParam("ssid", true)->value()
                                : String();
        const String pass = request->hasParam("pass", true)
                                ? request->getParam("pass", true)->value()
                                : String();

        StaticJsonDocument<320> patch;
        JsonObject root = patch.to<JsonObject>();
        JsonObject wifi = root.createNestedObject("wifi");
        wifi["enabled"] = enabled;
        wifi["ssid"] = ssid.c_str();
        wifi["pass"] = pass.c_str();

        char patchJson[320] = {0};
        if (serializeJson(patch, patchJson, sizeof(patchJson)) == 0) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        if (!cfgStore_->applyJson(patchJson)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.config.set\"}}");
            return;
        }

        if (!netAccessSvc_ && services_) {
            netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
        }
        if (netAccessSvc_ && netAccessSvc_->notifyWifiConfigChanged) {
            netAccessSvc_->notifyWifiConfigChanged(netAccessSvc_->ctx);
        }

        bool flowSyncAttempted = false;
        bool flowSyncOk = false;
        char flowSyncErr[96] = {0};
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (flowCfgSvc_ && flowCfgSvc_->applyPatchJson) {
            flowSyncAttempted = true;

            StaticJsonDocument<320> flowPatchDoc;
            JsonObject flowRoot = flowPatchDoc.to<JsonObject>();
            JsonObject flowWifi = flowRoot.createNestedObject("wifi");
            flowWifi["enabled"] = enabled;
            flowWifi["ssid"] = ssid.c_str();
            flowWifi["pass"] = pass.c_str();

            char flowPatchJson[320] = {0};
            const size_t flowPatchLen = serializeJson(flowPatchDoc, flowPatchJson, sizeof(flowPatchJson));
            if (flowPatchLen > 0 && flowPatchLen < sizeof(flowPatchJson)) {
                char flowAck[Limits::Mqtt::Buffers::Ack] = {0};
                flowSyncOk = flowCfgSvc_->applyPatchJson(flowCfgSvc_->ctx, flowPatchJson, flowAck, sizeof(flowAck));
                if (!flowSyncOk) {
                    snprintf(flowSyncErr, sizeof(flowSyncErr), "flowcfg.apply failed");
                }
            } else {
                snprintf(flowSyncErr, sizeof(flowSyncErr), "flowcfg.patch serialize failed");
            }
        } else {
            snprintf(flowSyncErr, sizeof(flowSyncErr), "flowcfg service unavailable");
        }

        if (flowSyncAttempted && flowSyncOk) {
            LOGI("WiFi config synced to Flow.IO");
        } else {
            LOGW("WiFi config sync to Flow.IO skipped/failed attempted=%d err=%s",
                 (int)flowSyncAttempted,
                 flowSyncErr[0] ? flowSyncErr : "none");
        }

        char out[256] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"flowio_sync\":{\"attempted\":%s,\"ok\":%s,\"err\":\"%s\"}}",
                               flowSyncAttempted ? "true" : "false",
                               flowSyncOk ? "true" : "false",
                               flowSyncErr);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/scan");
        if (!wifiSvc_ && services_) {
            wifiSvc_ = services_->get<WifiService>(ServiceId::Wifi);
        }
        if (!wifiSvc_ || !wifiSvc_->scanStatusJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.get\"}}");
            return;
        }

        char out[Limits::Wifi::Buffers::ScanStatusJson] = {0};
        if (!wifiSvc_->scanStatusJson(wifiSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.scan.get\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/wifi/scan", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/wifi/scan");
        if (!wifiSvc_ && services_) {
            wifiSvc_ = services_->get<WifiService>(ServiceId::Wifi);
        }
        if (!wifiSvc_ || !wifiSvc_->requestScan) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"wifi.scan.start\"}}");
            return;
        }

        String forceStr = request->hasParam("force", true)
                              ? request->getParam("force", true)->value()
                              : String("1");
        const bool force = parseBoolParam_(forceStr, true);
        if (!wifiSvc_->requestScan(wifiSvc_->ctx, force)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"wifi.scan.start\"}}");
            return;
        }

        if (wifiSvc_->scanStatusJson) {
            char out[Limits::Wifi::Buffers::ScanStatusJson] = {0};
            if (wifiSvc_->scanStatusJson(wifiSvc_->ctx, out, sizeof(out))) {
                request->send(202, "application/json", out);
                return;
            }
        }

        request->send(202, "application/json", "{\"ok\":true,\"accepted\":true}");
    });

    server_.on("/api/flow/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flow/status",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusDomainJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.link\"}}");
            return;
        }

        char domainBuf[640] = {0};
        StaticJsonDocument<768> domainDoc;
        StaticJsonDocument<1024> compactDoc;
        compactDoc["ok"] = true;

        bool anyDomainOk = false;
        auto loadDomain = [&](FlowStatusDomain domain) -> JsonObjectConst {
            memset(domainBuf, 0, sizeof(domainBuf));
            if (!flowCfgSvc_->runtimeStatusDomainJson(flowCfgSvc_->ctx, domain, domainBuf, sizeof(domainBuf))) {
                domainDoc.clear();
                return JsonObjectConst();
            }
            domainDoc.clear();
            const DeserializationError err = deserializeJson(domainDoc, domainBuf);
            if (err || !domainDoc.is<JsonObjectConst>()) {
                domainDoc.clear();
                return JsonObjectConst();
            }
            JsonObjectConst root = domainDoc.as<JsonObjectConst>();
            if (!(root["ok"] | false)) {
                domainDoc.clear();
                return JsonObjectConst();
            }
            anyDomainOk = true;
            return root;
        };

        {
            JsonObjectConst root = loadDomain(FlowStatusDomain::System);
            if (!root.isNull()) {
                compactDoc["fw"] = String(root["fw"] | "");
                compactDoc["upms"] = root["upms"] | 0U;
                JsonObject heapOut = compactDoc.createNestedObject("heap");
                JsonObjectConst heapIn = root["heap"];
                heapOut["free"] = heapIn["free"] | 0U;
                heapOut["min_free"] = heapIn["min_free"] | 0U;
            }
        }

        {
            JsonObjectConst root = loadDomain(FlowStatusDomain::Wifi);
            if (!root.isNull()) {
                JsonObject wifiOut = compactDoc.createNestedObject("wifi");
                JsonObjectConst wifiIn = root["wifi"];
                wifiOut["rdy"] = wifiIn["rdy"] | false;
                wifiOut["ip"] = String(wifiIn["ip"] | "");
                wifiOut["hrss"] = wifiIn["hrss"] | false;
                wifiOut["rssi"] = wifiIn["rssi"] | -127;
            }
        }

        {
            JsonObjectConst root = loadDomain(FlowStatusDomain::Mqtt);
            if (!root.isNull()) {
                JsonObject mqttOut = compactDoc.createNestedObject("mqtt");
                JsonObjectConst mqttIn = root["mqtt"];
                mqttOut["rdy"] = mqttIn["rdy"] | false;
                mqttOut["srv"] = String(mqttIn["srv"] | "");
                mqttOut["rxdrp"] = mqttIn["rxdrp"] | 0U;
                mqttOut["prsf"] = mqttIn["prsf"] | 0U;
                mqttOut["hndf"] = mqttIn["hndf"] | 0U;
                mqttOut["ovr"] = mqttIn["ovr"] | 0U;
            }
        }

        {
            JsonObjectConst root = loadDomain(FlowStatusDomain::Pool);
            if (!root.isNull()) {
                JsonObject poolOut = compactDoc.createNestedObject("pool");
                JsonObjectConst poolIn = root["pool"];
                poolOut["has"] = poolIn["has"] | false;
                poolOut["auto"] = poolIn["auto"] | false;
                poolOut["wint"] = poolIn["wint"] | false;
                poolOut["wat"] = poolIn["wat"];
                poolOut["air"] = poolIn["air"];
                poolOut["ph"] = poolIn["ph"];
                poolOut["orp"] = poolIn["orp"];
                poolOut["fil"] = poolIn["fil"];
                poolOut["php"] = poolIn["php"];
                poolOut["clp"] = poolIn["clp"];
                poolOut["rbt"] = poolIn["rbt"];
            }
        }

        {
            JsonObjectConst root = loadDomain(FlowStatusDomain::I2c);
            if (!root.isNull()) {
                JsonObject i2cOut = compactDoc.createNestedObject("i2c");
                JsonObjectConst i2cIn = root["i2c"];
                i2cOut["lnk"] = i2cIn["lnk"] | false;
                i2cOut["seen"] = i2cIn["seen"] | false;
                i2cOut["req"] = i2cIn["req"] | 0U;
                i2cOut["breq"] = i2cIn["breq"] | 0U;
                i2cOut["ago"] = i2cIn["ago"] | 0U;
            }
        }

        if (!anyDomainOk) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status\"}}");
            return;
        }

        char compactOut[1024] = {0};
        const size_t compactLen = serializeJson(compactDoc, compactOut, sizeof(compactOut));
        if (compactLen == 0 || compactLen >= sizeof(compactOut)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status.pack\"}}");
            return;
        }
        request->send(200, "application/json", compactOut);
    });

    server_.on("/api/flow/status/domain", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flow/status/domain",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusDomainJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.domain\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.status.domain.link\"}}");
            return;
        }

        if (!request->hasParam("d")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"flow.status.domain\"}}");
            return;
        }
        const AsyncWebParameter* domainParam = request->getParam("d");

        FlowStatusDomain domain = FlowStatusDomain::System;
        if (!parseFlowStatusDomainParam_(domainParam->value(), domain)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"BadDomain\",\"where\":\"flow.status.domain\"}}");
            return;
        }

        char domainBuf[640] = {0};
        if (!flowCfgSvc_->runtimeStatusDomainJson(flowCfgSvc_->ctx, domain, domainBuf, sizeof(domainBuf))) {
            if (domainBuf[0] != '\0') {
                request->send(500, "application/json", domainBuf);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.status.domain.fetch\"}}");
            }
            return;
        }

        request->send(200, "application/json", domainBuf);
    });

    server_.on("/api/runtime/manifest", HTTP_GET, [this, beginSpiffsAssetResponse](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/runtime/manifest");
        AsyncWebServerResponse* response =
            beginSpiffsAssetResponse(request, "/webinterface/runtimeui.json", "application/json", true);
        if (!response) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.manifest\"}}");
            return;
        }
        request->send(response);
    });

    server_.on("/api/runtime/alarms", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/runtime/alarms",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Disabled\",\"where\":\"runtime.alarms.disabled\"}}");
    });

    server_.on(
        "/api/runtime/values",
        HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            HttpLatencyScope latency(request,
                                     "/api/runtime/values",
                                     kHttpLatencyFlowCfgInfoMs,
                                     kHttpLatencyFlowCfgWarnMs);
            if (!flowCfgSvc_ && services_) {
                flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
            }
            if (!flowCfgSvc_ || !flowCfgSvc_->runtimeUiValues) {
                request->send(503, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values\"}}");
                return;
            }
            if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
                request->send(503, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"runtime.values.link\"}}");
                return;
            }
            if (!request->_tempObject) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.body\"}}");
                return;
            }

            char* body = static_cast<char*>(request->_tempObject);
            request->_tempObject = nullptr;

            DynamicJsonDocument reqDoc(2048);
            const DeserializationError reqErr = deserializeJson(reqDoc, body);
            free(body);
            if (reqErr) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.json\"}}");
                return;
            }

            JsonArrayConst idsIn = reqDoc["ids"].as<JsonArrayConst>();
            if (idsIn.isNull()) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"BadRequest\",\"where\":\"runtime.values.ids\"}}");
                return;
            }

            static constexpr size_t kMaxRuntimeHttpIds = 48U;
            RuntimeUiId ids[kMaxRuntimeHttpIds] = {};
            size_t idCount = 0U;
            for (JsonVariantConst item : idsIn) {
                if (!item.is<uint32_t>()) continue;
                if (idCount >= kMaxRuntimeHttpIds) break;
                const uint32_t raw = item.as<uint32_t>();
                if (raw == 0U || raw > 65535U) continue;
                ids[idCount++] = (RuntimeUiId)raw;
            }

            AsyncResponseStream* response = request->beginResponseStream("application/json");
            addNoCacheHeaders_(response);
            response->print("{\"ok\":true,\"values\":[");
            bool firstValue = true;

            size_t start = 0U;
            while (start < idCount) {
                size_t batchCount = 0U;
                size_t batchBudget = 1U;  // record count byte
                while ((start + batchCount) < idCount) {
                    const RuntimeUiManifestItem* item = findRuntimeUiManifestItem(ids[start + batchCount]);
                    const bool isString = item && item->type && strcmp(item->type, "string") == 0;
                    const size_t estimate = runtimeUiWireEstimate_(item);

                    if (batchCount > 0U && (isString || (batchBudget + estimate) > I2cCfgProtocol::MaxPayload)) {
                        break;
                    }
                    batchBudget += estimate;
                    ++batchCount;
                    if (isString) break;
                }
                if (batchCount == 0U) batchCount = 1U;

                uint8_t payload[I2cCfgProtocol::MaxPayload] = {0};
                size_t written = 0U;
                if (!flowCfgSvc_->runtimeUiValues(flowCfgSvc_->ctx,
                                                  ids + start,
                                                  (uint8_t)batchCount,
                                                  payload,
                                                  sizeof(payload),
                                                  &written)) {
                    delete response;
                    request->send(502, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"runtime.values.fetch\"}}");
                    return;
                }
                if (!appendRuntimeUiJsonValuesToStream_(*response, payload, written, firstValue)) {
                    delete response;
                    request->send(502, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"runtime.values.decode\"}}");
                    return;
                }
                start += batchCount;
            }

            response->print("]}");
            request->send(response);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static constexpr size_t kMaxBodyBytes = 2048U;
            if (index == 0U) {
                if (total == 0U || total > kMaxBodyBytes) {
                    request->send(413, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"ArgsTooLarge\",\"where\":\"runtime.values.body\"}}");
                    return;
                }
                char* body = static_cast<char*>(malloc(total + 1U));
                if (!body) {
                    request->send(500, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"runtime.values.alloc\"}}");
                    return;
                }
                request->_tempObject = body;
            }

            char* body = static_cast<char*>(request->_tempObject);
            if (!body) return;
            memcpy(body + index, data, len);
            if ((index + len) < total) return;
            body[total] = '\0';
        });

    server_.on("/api/flowcfg/modules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/modules",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->listModulesJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.modules\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.modules.link\"}}");
            return;
        }

        char out[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->listModulesJson(flowCfgSvc_->ctx, out, sizeof(out))) {
            if (out[0] != '\0') {
                LOGW("flowcfg.modules failed details=%s", out);
                request->send(500, "application/json", out);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.modules\"}}");
            }
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/flowcfg/children", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/children",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->listChildrenJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.children\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.children.link\"}}");
            return;
        }

        const String prefix = request->hasParam("prefix") ? request->getParam("prefix")->value() : "";
        char out[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->listChildrenJson(flowCfgSvc_->ctx, prefix.c_str(), out, sizeof(out))) {
            if (out[0] != '\0') {
                LOGW("flowcfg.children failed prefix=%s details=%s", prefix.c_str(), out);
                request->send(500, "application/json", out);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.children\"}}");
            }
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/flowcfg/module", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/module",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->getModuleJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.module\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.module.link\"}}");
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        String moduleStr = request->getParam("name")->value();
        if (moduleStr.length() == 0) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.module.name\"}}");
            return;
        }

        char moduleName[64] = {0};
        snprintf(moduleName, sizeof(moduleName), "%s", moduleStr.c_str());
        sanitizeJsonString_(moduleName);

        bool truncated = false;
        char moduleJson[Limits::Mqtt::Buffers::StateCfg] = {0};
        if (!flowCfgSvc_->getModuleJson(flowCfgSvc_->ctx, moduleStr.c_str(), moduleJson, sizeof(moduleJson), &truncated)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.module.get\"}}");
            return;
        }

        char out[Limits::Mqtt::Buffers::StateCfg + 128] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"module\":\"%s\",\"truncated\":%s,\"data\":%s}",
                               moduleName,
                               truncated ? "true" : "false",
                               moduleJson);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.module.pack\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/flowcfg/apply", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request,
                                 "/api/flowcfg/apply",
                                 kHttpLatencyFlowCfgInfoMs,
                                 kHttpLatencyFlowCfgWarnMs);
        if (!flowCfgSvc_ && services_) {
            flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
        }
        if (!flowCfgSvc_ || !flowCfgSvc_->applyPatchJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.apply\"}}");
            return;
        }
        if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flowcfg.apply.link\"}}");
            return;
        }
        if (!request->hasParam("patch", true)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"flowcfg.apply.patch\"}}");
            return;
        }

        String patchStr = request->getParam("patch", true)->value();
        char ack[Limits::Mqtt::Buffers::Ack] = {0};
        if (!flowCfgSvc_->applyPatchJson(flowCfgSvc_->ctx, patchStr.c_str(), ack, sizeof(ack))) {
            if (ack[0] != '\0') {
                request->send(flowCfgApplyHttpStatus_(ack), "application/json", ack);
            } else {
                request->send(500, "application/json",
                              "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flowcfg.apply.exec\"}}");
            }
            return;
        }
        request->send(200, "application/json", ack);
    });

    server_.on("/api/supervisorcfg/modules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/modules");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.modules\"}}");
            return;
        }

        constexpr uint8_t kMaxModules = 96;
        const char* modules[kMaxModules] = {0};
        const uint8_t moduleCount = cfgStore_->listModules(modules, kMaxModules);

        StaticJsonDocument<2048> doc;
        doc["ok"] = true;
        JsonArray arr = doc.createNestedArray("modules");
        for (uint8_t i = 0; i < moduleCount; ++i) {
            if (!modules[i] || modules[i][0] == '\0') continue;
            arr.add(modules[i]);
        }

        char out[2048] = {0};
        if (serializeJson(doc, out, sizeof(out)) == 0) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"supervisorcfg.modules\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/supervisorcfg/module", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/module");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.module\"}}");
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.module.name\"}}");
            return;
        }

        String moduleStr = request->getParam("name")->value();
        if (moduleStr.length() == 0) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.module.name\"}}");
            return;
        }

        char moduleName[64] = {0};
        snprintf(moduleName, sizeof(moduleName), "%s", moduleStr.c_str());
        sanitizeJsonString_(moduleName);

        bool truncated = false;
        char moduleJson[Limits::Mqtt::Buffers::StateCfg] = {0};
        if (!cfgStore_->toJsonModule(moduleStr.c_str(), moduleJson, sizeof(moduleJson), &truncated)) {
            request->send(404, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotFound\",\"where\":\"supervisorcfg.module.get\"}}");
            return;
        }

        char out[Limits::Mqtt::Buffers::StateCfg + 128] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"module\":\"%s\",\"truncated\":%s,\"data\":%s}",
                               moduleName,
                               truncated ? "true" : "false",
                               moduleJson);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"supervisorcfg.module.pack\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/supervisorcfg/apply", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/supervisorcfg/apply");
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"supervisorcfg.apply\"}}");
            return;
        }
        if (!request->hasParam("patch", true)) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"supervisorcfg.apply.patch\"}}");
            return;
        }

        String patchStr = request->getParam("patch", true)->value();
        if (!cfgStore_->applyJson(patchStr.c_str())) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"supervisorcfg.apply.exec\"}}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/system/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"system.reboot\"}}");
            return;
        }

        char reply[196] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "system.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            const String body = (reply[0] != '\0')
                                    ? String(reply)
                                    : String("{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"system.reboot\"}}");
            request->send(500, "application/json", body);
            return;
        }
        const String body = (reply[0] != '\0') ? String(reply) : String("{\"ok\":true}");
        request->send(200, "application/json", body);
    });

    server_.on("/api/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/system/factory-reset");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"system.factory_reset\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "system.factory_reset", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            const String body =
                (reply[0] != '\0')
                    ? String(reply)
                    : String("{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"system.factory_reset\"}}");
            request->send(500, "application/json", body);
            return;
        }
        const String body = (reply[0] != '\0') ? String(reply) : String("{\"ok\":true}");
        request->send(200, "application/json", body);
    });

    server_.on("/api/flow/system/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/flow/system/reboot");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.system.reboot\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "flow.system.reboot", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            const String body =
                (reply[0] != '\0')
                    ? String(reply)
                    : String("{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.system.reboot\"}}");
            request->send(500, "application/json", body);
            return;
        }
        const String body = (reply[0] != '\0') ? String(reply) : String("{\"ok\":true}");
        request->send(200, "application/json", body);
    });

    server_.on("/api/flow/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/api/flow/system/factory-reset");
        if (!cmdSvc_ && services_) {
            cmdSvc_ = services_->get<CommandService>(ServiceId::Command);
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"flow.system.factory_reset\"}}");
            return;
        }

        char reply[220] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "flow.system.factory_reset", "{}", nullptr, reply, sizeof(reply));
        if (!ok) {
            const String body =
                (reply[0] != '\0')
                    ? String(reply)
                    : String("{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"flow.system.factory_reset\"}}");
            request->send(500, "application/json", body);
            return;
        }
        const String body = (reply[0] != '\0') ? String(reply) : String("{\"ok\":true}");
        request->send(200, "application/json", body);
    });

    server_.on("/fwupdate/flowio", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/flowio");
        handleUpdateRequest_(request, FirmwareUpdateTarget::FlowIO);
    });

    server_.on("/fwupdate/supervisor", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/supervisor");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Supervisor);
    });

    server_.on("/fwupdate/nextion", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/nextion");
        handleUpdateRequest_(request, FirmwareUpdateTarget::Nextion);
    });
    server_.on("/fwupdate/cfgdocs", HTTP_POST, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/fwupdate/cfgdocs");
        handleUpdateRequest_(request, FirmwareUpdateTarget::CfgDocs);
    });

    server_.onNotFound([webInterfaceLandingUrl](AsyncWebServerRequest* request) {
        request->redirect(webInterfaceLandingUrl());
    });

    ws_.onEvent([this](AsyncWebSocket* server,
                       AsyncWebSocketClient* client,
                       AwsEventType type,
                       void* arg,
                       uint8_t* data,
                       size_t len) {
        this->onWsEvent_(server, client, type, arg, data, len);
    });
    wsLog_.onEvent([this](AsyncWebSocket* server,
                          AsyncWebSocketClient* client,
                          AwsEventType type,
                          void* arg,
                          uint8_t* data,
                          size_t len) {
        this->onWsLogEvent_(server, client, type, arg, data, len);
    });

    server_.addHandler(&ws_);
    server_.addHandler(&wsLog_);
    server_.begin();
    started_ = true;
    LOGI("WebInterface server started, listening on 0.0.0.0:%d", kServerPort);

    char ip[16] = {0};
    NetworkAccessMode mode = NetworkAccessMode::None;
    if (getNetworkIp_(ip, sizeof(ip), &mode) && ip[0] != '\0') {
        if (mode == NetworkAccessMode::AccessPoint) {
            LOGI("WebInterface URL (AP): http://%s/webinterface", ip);
        } else {
            LOGI("WebInterface URL: http://%s/webinterface", ip);
        }
    } else {
        LOGI("WebInterface URL: waiting for network IP");
    }
}

void WebInterfaceModule::handleUpdateRequest_(AsyncWebServerRequest* request, FirmwareUpdateTarget target)
{
    if (!request) return;
    if (!fwUpdateSvc_ && services_) {
        fwUpdateSvc_ = services_->get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    }
    if (!fwUpdateSvc_ || !fwUpdateSvc_->start) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    const AsyncWebParameter* pUrl = request->hasParam("url", true) ? request->getParam("url", true) : nullptr;
    String urlStr;
    if (pUrl) {
        urlStr = pUrl->value();
    }
    const char* url = (urlStr.length() > 0) ? urlStr.c_str() : nullptr;

    char err[144] = {0};
    if (!fwUpdateSvc_->start(fwUpdateSvc_->ctx, target, url, err, sizeof(err))) {
        request->send(409,
                      "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    request->send(202, "application/json", "{\"ok\":true,\"accepted\":true}");
}

bool WebInterfaceModule::isWebReachable_() const
{
    if (netAccessSvc_ && netAccessSvc_->isWebReachable) {
        return netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
    }
    if (wifiSvc_ && wifiSvc_->isConnected) {
        return wifiSvc_->isConnected(wifiSvc_->ctx);
    }
    return netReady_;
}

bool WebInterfaceModule::getNetworkIp_(char* out, size_t len, NetworkAccessMode* modeOut) const
{
    if (out && len > 0) out[0] = '\0';
    if (modeOut) *modeOut = NetworkAccessMode::None;
    if (!out || len == 0) return false;

    if (netAccessSvc_ && netAccessSvc_->getIP) {
        if (netAccessSvc_->getIP(netAccessSvc_->ctx, out, len)) {
            if (modeOut && netAccessSvc_->mode) {
                *modeOut = netAccessSvc_->mode(netAccessSvc_->ctx);
            }
            return out[0] != '\0';
        }
    }

    if (wifiSvc_ && wifiSvc_->getIP) {
        if (wifiSvc_->getIP(wifiSvc_->ctx, out, len)) {
            if (modeOut) *modeOut = NetworkAccessMode::Station;
            return out[0] != '\0';
        }
    }

    return false;
}
