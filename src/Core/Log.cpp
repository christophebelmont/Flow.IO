/**
 * @file Log.cpp
 * @brief Implementation file.
 */
#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include <stdarg.h>
#include <stdio.h>

namespace {
    const LogHubService* g_hub = nullptr;

    void logVaModule(LogLevel lvl, LogModuleId moduleId, const char* fmt, va_list ap) {
        if (!g_hub || !g_hub->enqueue || !fmt) return;
        if (g_hub->shouldLog && !g_hub->shouldLog(g_hub->ctx, moduleId, lvl)) return;

        LogEntry e{};
        e.ts_ms = millis();
        e.lvl = lvl;
        e.moduleId = moduleId;

        const int wrote = vsnprintf(e.msg, LOG_MSG_MAX, fmt, ap);
        if ((wrote < 0 || wrote >= LOG_MSG_MAX) && g_hub->noteFormatTruncation) {
            g_hub->noteFormatTruncation(g_hub->ctx, moduleId, (wrote < 0) ? 0U : (uint32_t)wrote);
        }
        g_hub->enqueue(g_hub->ctx, e);
    }
}

void Log::setHub(const LogHubService* hub) {
    g_hub = hub;
}

const LogHubService* Log::hub() {
    return g_hub;
}

bool Log::registerModule(LogModuleId moduleId, const char* moduleName) {
    if (!g_hub || !g_hub->registerModule) return false;
    return g_hub->registerModule(g_hub->ctx, moduleId, moduleName);
}

bool Log::setModuleMinLevel(LogModuleId moduleId, LogLevel level) {
    if (!g_hub || !g_hub->setModuleMinLevel) return false;
    return g_hub->setModuleMinLevel(g_hub->ctx, moduleId, level);
}

void Log::logf(LogLevel lvl, LogModuleId moduleId, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logVaModule(lvl, moduleId, fmt, ap);
    va_end(ap);
}

void Log::debug(LogModuleId moduleId, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logVaModule(LogLevel::Debug, moduleId, fmt, ap);
    va_end(ap);
}

void Log::info(LogModuleId moduleId, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logVaModule(LogLevel::Info, moduleId, fmt, ap);
    va_end(ap);
}

void Log::warn(LogModuleId moduleId, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logVaModule(LogLevel::Warn, moduleId, fmt, ap);
    va_end(ap);
}

void Log::error(LogModuleId moduleId, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logVaModule(LogLevel::Error, moduleId, fmt, ap);
    va_end(ap);
}
