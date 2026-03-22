/**
 * @file WebInterfaceModule.cpp
 * @brief Web interface bridge for Supervisor profile.
 */

#include "WebInterfaceModule.h"

#include "Board/BoardSpec.h"
#include "Core/FirmwareVersion.h"

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
#include "WebInterfaceMenuIcons.h"

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

void addVersionedAssetCacheHeaders_(AsyncWebServerResponse* response)
{
    if (!response) return;
    response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
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
    if (raw.equalsIgnoreCase("alarm")) {
        domainOut = FlowStatusDomain::Alarm;
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

    static WebInterfaceService webInterfaceSvc{
        &WebInterfaceModule::svcSetPaused_,
        &WebInterfaceModule::svcIsPaused_,
        nullptr
    };
    webInterfaceSvc.ctx = this;
    if (!services.add(ServiceId::WebInterface, &webInterfaceSvc)) {
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

    server_.on("/assets/icon-journaux.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconJournauxSvg);
    });
    server_.on("/assets/icon-status.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconStatusSvg);
    });
    server_.on("/assets/icon-upgrade.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconUpgradeSvg);
    });
    server_.on("/assets/icon-config.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconConfigSvg);
    });
    server_.on("/assets/icon-connections.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconConnectionsSvg);
    });
    server_.on("/assets/icon-system.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconSystemSvg);
    });
    server_.on("/assets/icon-control.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconControlSvg);
    });
    server_.on("/assets/icon-settings.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendProgmemLiteral_(request, "image/svg+xml", kMenuIconSettingsSvg);
    });

    server_.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });

    server_.on("/webinterface/app.css", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!spiffsReady_ || !SPIFFS.exists("/webinterface/app.css")) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/webinterface/app.css", "text/css");
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface/app.js", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!spiffsReady_ || !SPIFFS.exists("/webinterface/app.js")) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        AsyncWebServerResponse* response =
            request->beginResponse(SPIFFS, "/webinterface/app.js", "application/javascript");
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface/cfgdocs.fr.json", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (spiffsReady_ && SPIFFS.exists("/webinterface/cfgdocs.fr.json")) {
            AsyncWebServerResponse* response =
                request->beginResponse(SPIFFS, "/webinterface/cfgdocs.fr.json", "application/json");
            addVersionedAssetCacheHeaders_(response);
            request->send(response);
            return;
        }
        AsyncWebServerResponse* response =
            request->beginResponse(200, "application/json", "{\"_meta\":{\"generated\":false},\"docs\":{}}");
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface/cfgmods.fr.json", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (spiffsReady_ && SPIFFS.exists("/webinterface/cfgmods.fr.json")) {
            AsyncWebServerResponse* response =
                request->beginResponse(SPIFFS, "/webinterface/cfgmods.fr.json", "application/json");
            addVersionedAssetCacheHeaders_(response);
            request->send(response);
            return;
        }
        AsyncWebServerResponse* response =
            request->beginResponse(200, "application/json", "{\"_meta\":{\"generated\":false},\"docs\":{}}");
        addNoCacheHeaders_(response);
        request->send(response);
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
    server_.on("/webinterface", HTTP_GET, [this](AsyncWebServerRequest* request) {
        HttpLatencyScope latency(request, "/webinterface");
        if (spiffsReady_ && SPIFFS.exists("/webinterface/index.html")) {
            AsyncWebServerResponse* response =
                request->beginResponse(SPIFFS, "/webinterface/index.html", "text/html");
            addNoCacheHeaders_(response);
            request->send(response);
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html", kWebInterfaceFallbackPage);
        addNoCacheHeaders_(response);
        request->send(response);
    });
    server_.on("/webinterface/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });
    server_.on("/webserial", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
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
    server_.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });
    server_.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });
    server_.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });
    server_.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });
    server_.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
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

        char out[3072] = {0};
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
            char out[3072] = {0};
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

    server_.onNotFound([](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
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
            client->setCloseClientOnQueueFull(false);
            client->keepAlivePeriod(15);
            client->text("[webinterface] logs supervisor connectes");
        }
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

bool WebInterfaceModule::isLogByte_(uint8_t c)
{
    return c == '\t' || c == 0x1B || c >= 32;
}

char WebInterfaceModule::levelChar_(LogLevel lvl)
{
    if (lvl == LogLevel::Debug) return 'D';
    if (lvl == LogLevel::Info) return 'I';
    if (lvl == LogLevel::Warn) return 'W';
    if (lvl == LogLevel::Error) return 'E';
    return '?';
}

const char* WebInterfaceModule::levelColor_(LogLevel lvl)
{
    if (lvl == LogLevel::Debug) return "\x1b[90m";
    if (lvl == LogLevel::Info) return "\x1b[32m";
    if (lvl == LogLevel::Warn) return "\x1b[33m";
    if (lvl == LogLevel::Error) return "\x1b[31m";
    return "";
}

const char* WebInterfaceModule::colorReset_()
{
    return "\x1b[0m";
}

bool WebInterfaceModule::isSystemTimeValid_()
{
    time_t now = time(nullptr);
    return (now > 1609459200); // 2021-01-01 00:00:00
}

void WebInterfaceModule::formatUptime_(char* out, size_t outSize, uint32_t ms)
{
    uint32_t s = ms / 1000;
    uint32_t m = s / 60;
    uint32_t h = m / 60;
    const uint32_t hh = h % 24;
    const uint32_t mm = m % 60;
    const uint32_t ss = s % 60;
    const uint32_t mmm = ms % 1000;

    snprintf(out, outSize, "%02lu:%02lu:%02lu.%03lu",
             (unsigned long)hh,
             (unsigned long)mm,
             (unsigned long)ss,
             (unsigned long)mmm);
}

void WebInterfaceModule::formatTimestamp_(WebInterfaceModule* self, const LogEntry& e, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';

    bool timeFromService = false;
    if (self) {
        if (!self->timeSvc_ && self->services_) {
            self->timeSvc_ = self->services_->get<TimeService>(ServiceId::Time);
        }
        if (self->timeSvc_ &&
            self->timeSvc_->isSynced &&
            self->timeSvc_->formatLocalTime &&
            self->timeSvc_->isSynced(self->timeSvc_->ctx)) {
            char localTs[32] = {0};
            if (self->timeSvc_->formatLocalTime(self->timeSvc_->ctx, localTs, sizeof(localTs))) {
                const unsigned ms = e.ts_ms % 1000;
                snprintf(out, outSize, "%s.%03u", localTs, ms);
                timeFromService = true;
            }
        }
    }

    if (!timeFromService && isSystemTimeValid_()) {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        const unsigned ms = e.ts_ms % 1000;
        snprintf(out, outSize,
                 "%04d-%02d-%02d %02d:%02d:%02d.%03u",
                 t.tm_year + 1900,
                 t.tm_mon + 1,
                 t.tm_mday,
                 t.tm_hour,
                 t.tm_min,
                 t.tm_sec,
                 ms);
        return;
    }

    if (!timeFromService) {
        formatUptime_(out, outSize, e.ts_ms);
    }
}

void WebInterfaceModule::onLocalLogSinkWrite_(void* ctx, const LogEntry& e)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(ctx);
    if (!self || !self->localLogQueue_) return;
    const char* moduleName = nullptr;
    char moduleFallback[24] = {0};
    if (self->logHub_ && self->logHub_->resolveModuleName) {
        moduleName = self->logHub_->resolveModuleName(self->logHub_->ctx, e.moduleId);
    }
    if (!moduleName || moduleName[0] == '\0') {
        snprintf(moduleFallback, sizeof(moduleFallback), "#%u", (unsigned)e.moduleId);
        moduleName = moduleFallback;
    }
    const char* msg = e.msg;
    const char* color = levelColor_(e.lvl);
    char ts[48] = {0};
    formatTimestamp_(self, e, ts, sizeof(ts));

    char line[kLocalLogLineMax] = {0};
    int wrote = snprintf(line,
                         sizeof(line),
                         "[%s][%c][%s] %s%s",
                         ts,
                         levelChar_(e.lvl),
                         moduleName,
                         color,
                         msg);
    if (wrote > 0 && (size_t)wrote < sizeof(line)) {
        wrote += snprintf(line + wrote, sizeof(line) - (size_t)wrote, "%s", colorReset_());
    }
    if (wrote <= 0) return;
    if ((size_t)wrote >= sizeof(line)) {
        line[sizeof(line) - 1] = '\0';
    }

    if (xQueueSend(self->localLogQueue_, line, 0) != pdTRUE) {
        char dropped[kLocalLogLineMax] = {0};
        (void)xQueueReceive(self->localLogQueue_, dropped, 0);
        (void)xQueueSend(self->localLogQueue_, line, 0);
    }
}

bool WebInterfaceModule::svcSetPaused_(void* ctx, bool paused)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(ctx);
    if (!self) return false;
    self->uartPaused_ = paused;
    if (paused) {
        self->lineLen_ = 0;
    }
    return true;
}

bool WebInterfaceModule::svcIsPaused_(void* ctx)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(ctx);
    if (!self) return false;
    return self->uartPaused_;
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

void WebInterfaceModule::flushLocalLogQueue_()
{
    if (!started_ || !localLogQueue_) return;

    char line[kLocalLogLineMax] = {0};
    while (xQueueReceive(localLogQueue_, line, 0) == pdTRUE) {
        wsLog_.textAll(line);
    }
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

void WebInterfaceModule::loop()
{
    if (!netAccessSvc_ && services_) {
        netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
    }

    if (!started_) {
        if (!isWebReachable_()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }
        startServer_();
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
