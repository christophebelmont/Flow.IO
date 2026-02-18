/**
 * @file LogSerialSinkModule.cpp
 * @brief Implementation file.
 */
#include "LogSerialSinkModule.h"
#include <Arduino.h>
#include "Board/BoardSerialMap.h"
#include "Core/SnprintfCheck.h"

#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED("LogSerial", OUT, LEN, FMT, ##__VA_ARGS__)

struct SerialSinkCtx {
    ServiceRegistry* services = nullptr;
    const TimeService* timeSvc = nullptr;
};

static SerialSinkCtx gSerialSinkCtx{};
static HardwareSerial* gLogSerial = &Serial;

static const char* lvlStr(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return "D";
        case LogLevel::Info:  return "I";
        case LogLevel::Warn:  return "W";
        case LogLevel::Error: return "E";
    }
    return "?";
}

static const char* lvlColor(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return "\x1b[90m";
        case LogLevel::Info:  return "\x1b[32m";
        case LogLevel::Warn:  return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
    }
    return "";
}

static const char* colorReset() { return "\x1b[0m"; }

static bool isSystemTimeValid()
{
    // Classic ESP32 behavior: before real sync, epoch is near 1970.
    time_t now = time(nullptr);
    return (now > 1609459200); ///< 2021-01-01 00:00:00
}

static void formatUptime(char *out, size_t outSize, uint32_t ms)
{
    uint32_t s   = ms / 1000;
    uint32_t m   = s / 60;
    uint32_t h   = m / 60;

    uint32_t hh  = h % 24;
    uint32_t mm  = m % 60;
    uint32_t ss  = s % 60;
    uint32_t mmm = ms % 1000;

    snprintf(out, outSize, "%02lu:%02lu:%02lu.%03lu",
             (unsigned long)hh,
             (unsigned long)mm,
             (unsigned long)ss,
             (unsigned long)mmm);
}

static void serialSinkWrite(void* ctx, const LogEntry& e) {
    SerialSinkCtx* sinkCtx = static_cast<SerialSinkCtx*>(ctx);

    char ts[48];
    bool timeFromService = false;

    if (sinkCtx) {
        if (!sinkCtx->timeSvc && sinkCtx->services) {
            sinkCtx->timeSvc = sinkCtx->services->get<TimeService>("time");
        }

        if (sinkCtx->timeSvc &&
            sinkCtx->timeSvc->isSynced &&
            sinkCtx->timeSvc->formatLocalTime &&
            sinkCtx->timeSvc->isSynced(sinkCtx->timeSvc->ctx)) {
            char localTs[32] = {0};
            if (sinkCtx->timeSvc->formatLocalTime(sinkCtx->timeSvc->ctx, localTs, sizeof(localTs))) {
                unsigned ms = e.ts_ms % 1000;
                snprintf(ts, sizeof(ts), "%s.%03u", localTs, ms);
                timeFromService = true;
            }
        }
    }

    if (!timeFromService && isSystemTimeValid()) {
        // Real system time (NTP synced)
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);

        unsigned ms = e.ts_ms % 1000;

        snprintf(ts, sizeof(ts),
                 "%04d-%02d-%02d %02d:%02d:%02d.%03u",
                 t.tm_year + 1900,
                 t.tm_mon + 1,
                 t.tm_mday,
                 t.tm_hour,
                 t.tm_min,
                 t.tm_sec,
                 ms);
    } else if (!timeFromService) {
        // Fallback uptime
        formatUptime(ts, sizeof(ts), e.ts_ms);
    }

    const char* color = lvlColor(e.lvl);
    if (gLogSerial) {
        gLogSerial->printf("[%s][%s][%s] %s%s%s\n",
                           ts,
                           lvlStr(e.lvl),
                           e.tag,
                           color,
                           e.msg,
                           colorReset());
    }
}

void LogSerialSinkModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    (void)cfg;

    gLogSerial = &Board::SerialMap::logSerial();
    const int8_t rx = Board::SerialMap::logRxPin();
    const int8_t tx = Board::SerialMap::logTxPin();
    if (rx >= 0 && tx >= 0) {
        gLogSerial->begin(Board::SerialMap::LogBaud, SERIAL_8N1, rx, tx);
    } else {
        gLogSerial->begin(Board::SerialMap::LogBaud);
    }

    auto sinks = services.get<LogSinkRegistryService>("logsinks");
    if (!sinks) return;

    gSerialSinkCtx.services = &services;
    gSerialSinkCtx.timeSvc = nullptr;

    LogSinkService sink{};
    sink.write = serialSinkWrite;
    sink.ctx = &gSerialSinkCtx;

    sinks->add(sinks->ctx, sink);
}
