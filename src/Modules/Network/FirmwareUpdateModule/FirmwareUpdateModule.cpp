/**
 * @file FirmwareUpdateModule.cpp
 * @brief Supervisor firmware updater implementation.
 */

#include "FirmwareUpdateModule.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <FS.h>
#include <HTTPClient.h>
#include <Update.h>
#include <string.h>

#include "Board/BoardSpec.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"

#include <ESPNexUpload.h>
#include <esp32_flasher.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::FirmwareUpdateModule)
#include "Core/ModuleLog.h"

namespace {

const SupervisorBoardSpec& supervisorBoardSpec_(const BoardSpec& board)
{
    static constexpr SupervisorBoardSpec kFallback{
        {
            240,
            320,
            1,
            0,
            33,
            14,
            15,
            4,
            5,
            19,
            18,
            true,
            false,
            40000000U,
            80
        },
        {
            36,
            120,
            true,
            23,
            40
        },
        {
            25,
            26,
            13,
            115200U
        }
    };
    const SupervisorBoardSpec* cfg = boardSupervisorConfig(board);
    return cfg ? *cfg : kFallback;
}

const UartSpec& panelUartSpec_(const BoardSpec& board)
{
    static constexpr UartSpec kFallback{"panel", 2, 33, 32, 115200, false};
    const UartSpec* spec = boardFindUart(board, "panel");
    return spec ? *spec : kFallback;
}

}  // namespace

FirmwareUpdateModule::FirmwareUpdateModule(const BoardSpec& board)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    const UartSpec& panelUart = panelUartSpec_(board);
    flowIoEnablePin_ = boardCfg.update.flowIoEnablePin;
    flowIoBootPin_ = boardCfg.update.flowIoBootPin;
    nextionRxPin_ = panelUart.rxPin;
    nextionTxPin_ = panelUart.txPin;
    nextionRebootPin_ = boardCfg.update.nextionRebootPin;
    nextionUploadBaud_ = boardCfg.update.nextionUploadBaud;
}

static bool writeSimpleError_(char* out, size_t outLen, const char* msg)
{
    if (!out || outLen == 0) return false;
    if (!msg) msg = "failed";
    const int n = snprintf(out, outLen, "%s", msg);
    return n > 0 && (size_t)n < outLen;
}

static bool parseReqJsonObject_(const char* json, StaticJsonDocument<256>& doc)
{
    if (!json || json[0] == '\0') return false;
    const auto err = deserializeJson(doc, json);
    return !err && doc.is<JsonObjectConst>();
}

static void sanitizeJsonString_(char* s)
{
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') {
            s[i] = ' ';
        }
    }
}

static bool fileContainsToken_(fs::FS& fs, const char* path, const char* token)
{
    if (!path || !token || token[0] == '\0') return false;
    File f = fs.open(path, FILE_READ);
    if (!f) return false;

    const size_t tokLen = strlen(token);
    size_t match = 0;
    while (f.available()) {
        const int ch = f.read();
        if (ch < 0) break;
        if ((char)ch == token[match]) {
            ++match;
            if (match == tokLen) {
                f.close();
                return true;
            }
            continue;
        }
        match = ((char)ch == token[0]) ? 1U : 0U;
    }
    f.close();
    return false;
}

static bool validateCfgDocsFile_(fs::FS& fs, const char* path, char* errOut, size_t errOutLen)
{
    if (!path) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs path null");
        return false;
    }
    File f = fs.open(path, FILE_READ);
    if (!f) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs open failed");
        return false;
    }
    const size_t size = (size_t)f.size();
    f.close();
    if (size < 16U) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs too small");
        return false;
    }
    if (!fileContainsToken_(fs, path, "\"docs\"")) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs missing docs");
        return false;
    }
    return true;
}

static void configureDownloadHttp_(HTTPClient& http)
{
    http.setReuse(false);
    http.setConnectTimeout(Limits::FirmwareUpdate::Http::ConnectTimeoutMs);
    http.setTimeout(Limits::FirmwareUpdate::Http::RequestTimeoutMs);
}

const char* FirmwareUpdateModule::stateStr_(UpdateState s)
{
    switch (s) {
        case UpdateState::Idle: return "idle";
        case UpdateState::Queued: return "queued";
        case UpdateState::Downloading: return "downloading";
        case UpdateState::Flashing: return "flashing";
        case UpdateState::Done: return "done";
        case UpdateState::Error: return "error";
        default: return "unknown";
    }
}

const char* FirmwareUpdateModule::targetStr_(FirmwareUpdateTarget t)
{
    switch (t) {
        case FirmwareUpdateTarget::FlowIO: return "flowio";
        case FirmwareUpdateTarget::Nextion: return "nextion";
        case FirmwareUpdateTarget::Supervisor: return "supervisor";
        case FirmwareUpdateTarget::Spiffs: return "spiffs";
        default: return "unknown";
    }
}

void FirmwareUpdateModule::setStatus_(UpdateState state, FirmwareUpdateTarget target, uint8_t progress, const char* msg)
{
    portENTER_CRITICAL(&lock_);
    status_.state = state;
    status_.target = target;
    status_.progress = progress;
    status_.updatedAtMs = millis();
    if (!msg) msg = "";
    snprintf(status_.msg, sizeof(status_.msg), "%s", msg);
    portEXIT_CRITICAL(&lock_);
}

void FirmwareUpdateModule::setError_(FirmwareUpdateTarget target, const char* msg)
{
    setStatus_(UpdateState::Error, target, 0, msg ? msg : "failed");
}

void FirmwareUpdateModule::onProgressChunk_(uint32_t chunkBytes)
{
    portENTER_CRITICAL(&lock_);
    if (activeTotalBytes_ == 0) {
        portEXIT_CRITICAL(&lock_);
        return;
    }
    uint32_t next = activeSentBytes_ + chunkBytes;
    if (next > activeTotalBytes_) next = activeTotalBytes_;
    activeSentBytes_ = next;
    status_.progress = (uint8_t)((activeSentBytes_ * 100U) / activeTotalBytes_);
    status_.updatedAtMs = millis();
    portEXIT_CRITICAL(&lock_);
}

void FirmwareUpdateModule::attachWebInterfaceSvcIfNeeded_()
{
    if (webInterfaceSvc_ || !services_) return;
    webInterfaceSvc_ = services_->get<WebInterfaceService>(ServiceId::WebInterface);
}

bool FirmwareUpdateModule::resolveUrl_(FirmwareUpdateTarget target,
                                       const char* explicitUrl,
                                       char* out,
                                       size_t outLen,
                                       char* errOut,
                                       size_t errOutLen) const
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    if (explicitUrl && explicitUrl[0] != '\0') {
        const int n = snprintf(out, outLen, "%s", explicitUrl);
        if (n <= 0 || (size_t)n >= outLen) {
            writeSimpleError_(errOut, errOutLen, "url too long");
            return false;
        }
        return true;
    }

    if (cfgData_.updateHost[0] == '\0') {
        writeSimpleError_(errOut, errOutLen, "update_host empty");
        return false;
    }

    const char* path = nullptr;
    switch (target) {
        case FirmwareUpdateTarget::FlowIO:
            path = cfgData_.flowioPath;
            break;
        case FirmwareUpdateTarget::Supervisor:
            path = cfgData_.supervisorPath;
            break;
        case FirmwareUpdateTarget::Nextion:
            path = cfgData_.nextionPath;
            break;
        case FirmwareUpdateTarget::Spiffs:
            path = cfgData_.spiffsPath;
            break;
        default:
            break;
    }
    if (!path || path[0] == '\0') {
        writeSimpleError_(errOut, errOutLen, "path empty");
        return false;
    }

    const bool hasProto =
        (strncmp(cfgData_.updateHost, "http://", 7) == 0) || (strncmp(cfgData_.updateHost, "https://", 8) == 0);
    const char slash = (path[0] == '/') ? '\0' : '/';
    const int n = hasProto
                      ? ((slash != '\0') ? snprintf(out, outLen, "%s%c%s", cfgData_.updateHost, slash, path)
                                         : snprintf(out, outLen, "%s%s", cfgData_.updateHost, path))
                      : ((slash != '\0') ? snprintf(out, outLen, "http://%s%c%s", cfgData_.updateHost, slash, path)
                                         : snprintf(out, outLen, "http://%s%s", cfgData_.updateHost, path));
    if (n <= 0 || (size_t)n >= outLen) {
        writeSimpleError_(errOut, errOutLen, "resolved url too long");
        return false;
    }
    return true;
}

bool FirmwareUpdateModule::parseUrlArg_(const CommandRequest& req, char* out, size_t outLen) const
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    StaticJsonDocument<256> doc;
    if (parseReqJsonObject_(req.args, doc)) {
        const char* url = doc["url"] | nullptr;
        if (url && url[0] != '\0') {
            snprintf(out, outLen, "%s", url);
            return true;
        }
    }

    doc.clear();
    if (parseReqJsonObject_(req.json, doc)) {
        const char* rootUrl = doc["url"] | nullptr;
        if (rootUrl && rootUrl[0] != '\0') {
            snprintf(out, outLen, "%s", rootUrl);
            return true;
        }
        JsonVariantConst args = doc["args"];
        if (args.is<JsonObjectConst>()) {
            const char* nestedUrl = args["url"] | nullptr;
            if (nestedUrl && nestedUrl[0] != '\0') {
                snprintf(out, outLen, "%s", nestedUrl);
                return true;
            }
        }
    }

    return false;
}

bool FirmwareUpdateModule::statusJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;

    UpdateStatus snap{};
    bool busy = false;
    bool pending = false;
    portENTER_CRITICAL(&lock_);
    snap = status_;
    busy = busy_;
    pending = queuedJob_.pending;
    portEXIT_CRITICAL(&lock_);

    sanitizeJsonString_(snap.msg);

    const int n = snprintf(out,
                           outLen,
                           "{\"ok\":true,\"state\":\"%s\",\"target\":\"%s\",\"busy\":%s,"
                           "\"pending\":%s,\"progress\":%u,\"ts_ms\":%lu,\"msg\":\"%s\"}",
                           stateStr_(snap.state),
                           targetStr_(snap.target),
                           busy ? "true" : "false",
                           pending ? "true" : "false",
                           (unsigned)snap.progress,
                           (unsigned long)snap.updatedAtMs,
                           snap.msg);
    return n > 0 && (size_t)n < outLen;
}

bool FirmwareUpdateModule::configJson_(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return false;

    char host[sizeof(cfgData_.updateHost)] = {0};
    char flowPath[sizeof(cfgData_.flowioPath)] = {0};
    char supPath[sizeof(cfgData_.supervisorPath)] = {0};
    char nxPath[sizeof(cfgData_.nextionPath)] = {0};
    char spiffsPath[sizeof(cfgData_.spiffsPath)] = {0};
    snprintf(host, sizeof(host), "%s", cfgData_.updateHost);
    snprintf(flowPath, sizeof(flowPath), "%s", cfgData_.flowioPath);
    snprintf(supPath, sizeof(supPath), "%s", cfgData_.supervisorPath);
    snprintf(nxPath, sizeof(nxPath), "%s", cfgData_.nextionPath);
    snprintf(spiffsPath, sizeof(spiffsPath), "%s", cfgData_.spiffsPath);
    sanitizeJsonString_(host);
    sanitizeJsonString_(flowPath);
    sanitizeJsonString_(supPath);
    sanitizeJsonString_(nxPath);
    sanitizeJsonString_(spiffsPath);

    const int n = snprintf(out,
                           outLen,
                           "{\"ok\":true,\"update_host\":\"%s\",\"flowio_path\":\"%s\","
                           "\"supervisor_path\":\"%s\",\"nextion_path\":\"%s\","
                           "\"spiffs_path\":\"%s\",\"cfgdocs_path\":\"%s\"}",
                           host,
                           flowPath,
                           supPath,
                           nxPath,
                           spiffsPath,
                           spiffsPath);
    return n > 0 && (size_t)n < outLen;
}

bool FirmwareUpdateModule::setConfig_(const char* updateHost,
                                      const char* flowioPath,
                                      const char* supervisorPath,
                                      const char* nextionPath,
                                      const char* spiffsPath,
                                      char* errOut,
                                      size_t errOutLen)
{
    if (!cfgStore_) {
        writeSimpleError_(errOut, errOutLen, "config store unavailable");
        return false;
    }

    bool isBusy = false;
    bool hasPending = false;
    portENTER_CRITICAL(&lock_);
    isBusy = busy_;
    hasPending = queuedJob_.pending;
    portEXIT_CRITICAL(&lock_);
    if (isBusy || hasPending) {
        writeSimpleError_(errOut, errOutLen, "updater busy");
        return false;
    }

    if (updateHost) {
        if (!cfgStore_->set(updateHostVar_, updateHost)) {
            writeSimpleError_(errOut, errOutLen, "set update_host failed");
            return false;
        }
    }
    if (flowioPath) {
        if (!cfgStore_->set(flowioPathVar_, flowioPath)) {
            writeSimpleError_(errOut, errOutLen, "set flowio_path failed");
            return false;
        }
    }
    if (supervisorPath) {
        if (!cfgStore_->set(supervisorPathVar_, supervisorPath)) {
            writeSimpleError_(errOut, errOutLen, "set supervisor_path failed");
            return false;
        }
    }
    if (nextionPath) {
        if (!cfgStore_->set(nextionPathVar_, nextionPath)) {
            writeSimpleError_(errOut, errOutLen, "set nextion_path failed");
            return false;
        }
    }
    if (spiffsPath) {
        if (!cfgStore_->set(spiffsPathVar_, spiffsPath)) {
            writeSimpleError_(errOut, errOutLen, "set spiffs_path failed");
            return false;
        }
    }

    return true;
}

bool FirmwareUpdateModule::startUpdate_(FirmwareUpdateTarget target,
                                        const char* url,
                                        char* errOut,
                                        size_t errOutLen)
{
    UpdateJob job{};
    job.target = target;
    if (!resolveUrl_(target, url, job.url, sizeof(job.url), errOut, errOutLen)) {
        return false;
    }

    portENTER_CRITICAL(&lock_);
    if (busy_ || queuedJob_.pending) {
        portEXIT_CRITICAL(&lock_);
        writeSimpleError_(errOut, errOutLen, "updater busy");
        return false;
    }
    queuedJob_ = job;
    queuedJob_.pending = true;
    portEXIT_CRITICAL(&lock_);

    setStatus_(UpdateState::Queued, target, 0, "queued");
    LOGI("Update queued target=%s url=%s", targetStr_(target), job.url);
    return true;
}

bool FirmwareUpdateModule::runFlowIoUpdate_(const char* url, char* errOut, size_t errOutLen)
{
    if (flowIoBootPin_ < 0 || flowIoEnablePin_ < 0) {
        writeSimpleError_(errOut, errOutLen, "flowio board pins not configured");
        return false;
    }

    setStatus_(UpdateState::Downloading, FirmwareUpdateTarget::FlowIO, 0, "downloading");

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeSimpleError_(errOut, errOutLen, "http begin failed");
        return false;
    }

    const int code = http.GET();
    const int32_t contentLength = http.getSize();
    if (code != HTTP_CODE_OK) {
        char msg[96] = {0};
        snprintf(msg, sizeof(msg), "http %d: %s", code, http.errorToString(code).c_str());
        writeSimpleError_(errOut, errOutLen, msg);
        http.end();
        return false;
    }
    if (contentLength <= 0) {
        writeSimpleError_(errOut, errOutLen, "invalid content-length");
        http.end();
        return false;
    }

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::FlowIO, 0, "flashing");
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = (uint32_t)contentLength;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    attachWebInterfaceSvcIfNeeded_();
    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, true);
    }

    bool ok = false;
    ESP32Flasher flasher(flowIoBootPin_, flowIoEnablePin_);
    flasher.setUpdateProgressCallback([this]() {
        this->onProgressChunk_(1024U);
    });
    flasher.espFlasherInit();

    const int connectStatus = flasher.espConnect();
    if (connectStatus != SUCCESS) {
        char msg[64] = {0};
        snprintf(msg, sizeof(msg), "target connect failed (%d)", connectStatus);
        writeSimpleError_(errOut, errOutLen, msg);
    } else {
        const int flashStatus = flasher.espFlashBinStream(*http.getStreamPtr(), (uint32_t)contentLength);
        if (flashStatus != SUCCESS) {
            char msg[64] = {0};
            snprintf(msg, sizeof(msg), "stream flash failed (%d)", flashStatus);
            writeSimpleError_(errOut, errOutLen, msg);
        } else {
            ok = true;
        }
    }

    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, false);
    }

    http.end();
    if (!ok) return false;

    setStatus_(UpdateState::Done, FirmwareUpdateTarget::FlowIO, 100, "flowio update complete");
    return true;
}

bool FirmwareUpdateModule::runSupervisorUpdate_(const char* url, char* errOut, size_t errOutLen)
{
    setStatus_(UpdateState::Downloading, FirmwareUpdateTarget::Supervisor, 0, "downloading");

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeSimpleError_(errOut, errOutLen, "http begin failed");
        return false;
    }

    const int code = http.GET();
    const int32_t contentLength = http.getSize();
    if (code != HTTP_CODE_OK) {
        char msg[96] = {0};
        snprintf(msg, sizeof(msg), "http %d: %s", code, http.errorToString(code).c_str());
        writeSimpleError_(errOut, errOutLen, msg);
        http.end();
        return false;
    }

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Supervisor, 0, "flashing");
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = (contentLength > 0) ? (uint32_t)contentLength : 0U;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    attachWebInterfaceSvcIfNeeded_();
    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, true);
    }

    char failMsg[128] = {0};
    const size_t beginSize = (contentLength > 0) ? (size_t)contentLength : (size_t)UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(beginSize, U_FLASH)) {
        snprintf(failMsg, sizeof(failMsg), "ota begin failed (%u)", (unsigned)Update.getError());
    } else {
        WiFiClient* stream = http.getStreamPtr();
        int32_t remaining = contentLength;
        uint8_t buf[Limits::FirmwareUpdate::Http::StreamChunkBytes];
        uint32_t lastReadMs = millis();

        while (http.connected() && (contentLength <= 0 || remaining > 0)) {
            const size_t avail = stream ? stream->available() : 0;
            if (avail == 0U) {
                if (contentLength <= 0 && stream && !stream->connected()) {
                    break;
                }
                if ((millis() - lastReadMs) > Limits::FirmwareUpdate::Http::StreamReadTimeoutMs) {
                    snprintf(failMsg, sizeof(failMsg), "ota stream timeout");
                    break;
                }
                delay(1);
                continue;
            }

            const size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
            const int rd = stream->readBytes((char*)buf, toRead);
            if (rd <= 0) {
                delay(1);
                continue;
            }
            lastReadMs = millis();

            const size_t wr = Update.write(buf, (size_t)rd);
            if (wr != (size_t)rd) {
                snprintf(failMsg, sizeof(failMsg), "ota write failed (%u)", (unsigned)Update.getError());
                break;
            }

            onProgressChunk_((uint32_t)wr);

            if (contentLength > 0) {
                remaining -= rd;
                if (remaining <= 0) {
                    break;
                }
            }
        }

        if (failMsg[0] == '\0' && contentLength > 0 && remaining > 0) {
            snprintf(failMsg, sizeof(failMsg), "incomplete download");
        }
        if (failMsg[0] == '\0' && !Update.end()) {
            snprintf(failMsg, sizeof(failMsg), "ota end failed (%u)", (unsigned)Update.getError());
        }
        if (failMsg[0] == '\0' && !Update.isFinished()) {
            snprintf(failMsg, sizeof(failMsg), "ota not finished");
        }
    }

    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, false);
    }

    http.end();

    if (failMsg[0] != '\0') {
        writeSimpleError_(errOut, errOutLen, failMsg);
        return false;
    }

    setStatus_(UpdateState::Done, FirmwareUpdateTarget::Supervisor, 100, "supervisor update complete; rebooting");
    delay(700);
    ESP.restart();
    return true;
}

bool FirmwareUpdateModule::runNextionUpdate_(const char* url, char* errOut, size_t errOutLen)
{
    if (flowIoEnablePin_ < 0 || nextionRebootPin_ < 0 || nextionRxPin_ < 0 || nextionTxPin_ < 0) {
        writeSimpleError_(errOut, errOutLen, "nextion board pins not configured");
        return false;
    }

    setStatus_(UpdateState::Downloading, FirmwareUpdateTarget::Nextion, 0, "downloading");

    pinMode(flowIoEnablePin_, OUTPUT);
    digitalWrite(flowIoEnablePin_, LOW);

    pinMode(nextionRebootPin_, OUTPUT);
    digitalWrite(nextionRebootPin_, HIGH);

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeSimpleError_(errOut, errOutLen, "http begin failed");
        digitalWrite(flowIoEnablePin_, HIGH);
        pinMode(flowIoEnablePin_, INPUT);
        return false;
    }

    const int code = http.GET();
    const int32_t contentLength = http.getSize();
    if (code != HTTP_CODE_OK) {
        char msg[96] = {0};
        snprintf(msg, sizeof(msg), "http %d: %s", code, http.errorToString(code).c_str());
        writeSimpleError_(errOut, errOutLen, msg);
        http.end();
        digitalWrite(flowIoEnablePin_, HIGH);
        pinMode(flowIoEnablePin_, INPUT);
        return false;
    }
    if (contentLength <= 0) {
        writeSimpleError_(errOut, errOutLen, "invalid content-length");
        http.end();
        digitalWrite(flowIoEnablePin_, HIGH);
        pinMode(flowIoEnablePin_, INPUT);
        return false;
    }

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Nextion, 0, "flashing");
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = (uint32_t)contentLength;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    bool ok = false;
    ESPNexUpload nextion(nextionUploadBaud_, nextionRxPin_, nextionTxPin_);
    nextion.setUpdateProgressCallback([this]() {
        this->onProgressChunk_(2048U);
    });

    if (!nextion.prepareUpload((uint32_t)contentLength)) {
        writeSimpleError_(errOut, errOutLen, nextion.statusMessage.c_str());
    } else if (!nextion.upload(*http.getStreamPtr())) {
        writeSimpleError_(errOut, errOutLen, nextion.statusMessage.c_str());
    } else {
        ok = true;
    }
    nextion.end();

    pinMode(nextionRxPin_, INPUT);
    pinMode(nextionTxPin_, INPUT);

    http.end();
    digitalWrite(flowIoEnablePin_, HIGH);
    pinMode(flowIoEnablePin_, INPUT);

    if (!ok) return false;

    setStatus_(UpdateState::Done, FirmwareUpdateTarget::Nextion, 100, "nextion update complete");
    return true;
}

bool FirmwareUpdateModule::runSpiffsUpdate_(const char* url, char* errOut, size_t errOutLen)
{
    setStatus_(UpdateState::Downloading, FirmwareUpdateTarget::Spiffs, 0, "downloading");

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeSimpleError_(errOut, errOutLen, "http begin failed");
        return false;
    }

    const int code = http.GET();
    const int32_t contentLength = http.getSize();
    if (code != HTTP_CODE_OK) {
        char msg[96] = {0};
        snprintf(msg, sizeof(msg), "http %d: %s", code, http.errorToString(code).c_str());
        writeSimpleError_(errOut, errOutLen, msg);
        http.end();
        return false;
    }

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Spiffs, 0, "flashing spiffs");
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = (contentLength > 0) ? (uint32_t)contentLength : 0U;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    attachWebInterfaceSvcIfNeeded_();
    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, true);
    }

    char failMsg[128] = {0};
    const size_t beginSize = (contentLength > 0) ? (size_t)contentLength : (size_t)UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(beginSize, U_SPIFFS)) {
        snprintf(failMsg, sizeof(failMsg), "spiffs begin failed (%u)", (unsigned)Update.getError());
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[Limits::FirmwareUpdate::Http::StreamChunkBytes];
    int32_t remaining = contentLength;
    uint32_t lastReadMs = millis();
    if (failMsg[0] == '\0') {
        while (http.connected() && (contentLength <= 0 || remaining > 0)) {
            const size_t avail = stream ? stream->available() : 0;
            if (avail == 0U) {
                if (contentLength <= 0 && stream && !stream->connected()) {
                    break;
                }
                if ((millis() - lastReadMs) > Limits::FirmwareUpdate::Http::StreamReadTimeoutMs) {
                    snprintf(failMsg, sizeof(failMsg), "spiffs stream timeout");
                    break;
                }
                delay(1);
                continue;
            }

            const size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
            const int rd = stream->readBytes((char*)buf, toRead);
            if (rd <= 0) {
                delay(1);
                continue;
            }
            lastReadMs = millis();

            const size_t wr = Update.write(buf, (size_t)rd);
            if (wr != (size_t)rd) {
                snprintf(failMsg, sizeof(failMsg), "spiffs write failed (%u)", (unsigned)Update.getError());
                break;
            }

            onProgressChunk_((uint32_t)wr);

            if (contentLength > 0) {
                remaining -= rd;
                if (remaining <= 0) break;
            }
        }
    }
    http.end();

    if (failMsg[0] == '\0' && contentLength > 0 && remaining > 0) {
        snprintf(failMsg, sizeof(failMsg), "incomplete download");
    }
    if (failMsg[0] == '\0' && !Update.end()) {
        snprintf(failMsg, sizeof(failMsg), "spiffs end failed (%u)", (unsigned)Update.getError());
    }
    if (failMsg[0] == '\0' && !Update.isFinished()) {
        snprintf(failMsg, sizeof(failMsg), "spiffs not finished");
    }

    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, false);
    }

    if (failMsg[0] != '\0') {
        writeSimpleError_(errOut, errOutLen, failMsg);
        return false;
    }

    setStatus_(UpdateState::Done, FirmwareUpdateTarget::Spiffs, 100, "spiffs update complete; rebooting");
    delay(700);
    ESP.restart();
    return true;
}

bool FirmwareUpdateModule::runJob_(const UpdateJob& job)
{
    if (!wifiSvc_ || !wifiSvc_->isConnected || !wifiSvc_->isConnected(wifiSvc_->ctx)) {
        setError_(job.target, "wifi not connected");
        return false;
    }

    char err[128] = {0};
    bool ok = false;
    switch (job.target) {
        case FirmwareUpdateTarget::FlowIO:
            ok = runFlowIoUpdate_(job.url, err, sizeof(err));
            break;
        case FirmwareUpdateTarget::Supervisor:
            ok = runSupervisorUpdate_(job.url, err, sizeof(err));
            break;
        case FirmwareUpdateTarget::Nextion:
            ok = runNextionUpdate_(job.url, err, sizeof(err));
            break;
        case FirmwareUpdateTarget::Spiffs:
            ok = runSpiffsUpdate_(job.url, err, sizeof(err));
            break;
        default:
            snprintf(err, sizeof(err), "unsupported target");
            ok = false;
            break;
    }

    if (!ok) {
        setError_(job.target, err[0] ? err : "update failed");
        LOGE("Update failed target=%s reason=%s", targetStr_(job.target), err[0] ? err : "unknown");
        return false;
    }

    LOGI("Update done target=%s", targetStr_(job.target));
    return true;
}

bool FirmwareUpdateModule::cmdStatus_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;
    if (!self->statusJson_(reply, replyLen)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.status")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    return true;
}

bool FirmwareUpdateModule::cmdFlowIo_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char url[kUrlLen] = {0};
    const char* explicitUrl = self->parseUrlArg_(req, url, sizeof(url)) ? url : nullptr;
    char err[120] = {0};
    if (!self->startUpdate_(FirmwareUpdateTarget::FlowIO, explicitUrl, err, sizeof(err))) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.flowio")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"flowio\"}");
    return true;
}

bool FirmwareUpdateModule::cmdSupervisor_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char url[kUrlLen] = {0};
    const char* explicitUrl = self->parseUrlArg_(req, url, sizeof(url)) ? url : nullptr;
    char err[120] = {0};
    if (!self->startUpdate_(FirmwareUpdateTarget::Supervisor, explicitUrl, err, sizeof(err))) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.supervisor")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"supervisor\"}");
    return true;
}

bool FirmwareUpdateModule::cmdNextion_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char url[kUrlLen] = {0};
    const char* explicitUrl = self->parseUrlArg_(req, url, sizeof(url)) ? url : nullptr;
    char err[120] = {0};
    if (!self->startUpdate_(FirmwareUpdateTarget::Nextion, explicitUrl, err, sizeof(err))) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.nextion")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"nextion\"}");
    return true;
}

bool FirmwareUpdateModule::cmdSpiffs_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char url[kUrlLen] = {0};
    const char* explicitUrl = self->parseUrlArg_(req, url, sizeof(url)) ? url : nullptr;
    char err[120] = {0};
    if (!self->startUpdate_(FirmwareUpdateTarget::Spiffs, explicitUrl, err, sizeof(err))) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.spiffs")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"spiffs\"}");
    return true;
}

void FirmwareUpdateModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    services_ = &services;
    cfgStore_ = &cfg;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    webInterfaceSvc_ = services.get<WebInterfaceService>(ServiceId::WebInterface);

    cfg.registerVar(updateHostVar_);
    cfg.registerVar(flowioPathVar_);
    cfg.registerVar(supervisorPathVar_);
    cfg.registerVar(nextionPathVar_);
    cfg.registerVar(spiffsPathVar_);

    if (!services.add(ServiceId::FirmwareUpdate, &firmwareUpdateSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::FirmwareUpdate));
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.status", &FirmwareUpdateModule::cmdStatus_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.flowio", &FirmwareUpdateModule::cmdFlowIo_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.supervisor", &FirmwareUpdateModule::cmdSupervisor_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.nextion", &FirmwareUpdateModule::cmdNextion_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.spiffs", &FirmwareUpdateModule::cmdSpiffs_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.cfgdocs", &FirmwareUpdateModule::cmdSpiffs_, this);
    }

    setStatus_(UpdateState::Idle, FirmwareUpdateTarget::FlowIO, 0, "idle");
    LOGI("Firmware updater ready");
}

void FirmwareUpdateModule::loop()
{
    UpdateJob job{};

    portENTER_CRITICAL(&lock_);
    if (!queuedJob_.pending || busy_) {
        portEXIT_CRITICAL(&lock_);
        vTaskDelay(pdMS_TO_TICKS(60));
        return;
    }
    busy_ = true;
    job = queuedJob_;
    queuedJob_.pending = false;
    portEXIT_CRITICAL(&lock_);

    runJob_(job);

    portENTER_CRITICAL(&lock_);
    busy_ = false;
    activeTotalBytes_ = 0;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    vTaskDelay(pdMS_TO_TICKS(20));
}
