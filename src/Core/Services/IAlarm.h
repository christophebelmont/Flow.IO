#pragma once
/**
 * @file IAlarm.h
 * @brief Alarm engine service interface.
 */

#include <stddef.h>
#include <stdint.h>

#include "Core/AlarmIds.h"

/** Tri-state condition result returned by alarm callbacks. */
enum class AlarmCondState : uint8_t {
    False = 0,
    True = 1,
    Unknown = 2,
};

/** Callback used by modules to evaluate alarm conditions. */
using AlarmCondFn = AlarmCondState (*)(void* ctx, uint32_t nowMs);

/** Alarm definition registered by owner modules during init. */
struct AlarmRegistration {
    AlarmId id = AlarmId::None;
    AlarmSeverity severity = AlarmSeverity::Info;
    bool latched = false;
    uint32_t onDelayMs = 0;
    uint32_t offDelayMs = 0;
    uint32_t minRepeatMs = 0;
    char code[24] = {0};
    char title[48] = {0};
    char sourceModule[16] = {0};
};

/** Service contract exposed by AlarmModule. */
struct AlarmService {
    bool (*registerAlarm)(void* ctx, const AlarmRegistration* def, AlarmCondFn condFn, void* condCtx);
    bool (*ack)(void* ctx, AlarmId id);
    uint8_t (*ackAll)(void* ctx);
    bool (*isActive)(void* ctx, AlarmId id);
    bool (*isAcked)(void* ctx, AlarmId id);
    uint8_t (*activeCount)(void* ctx);
    AlarmSeverity (*highestSeverity)(void* ctx);
    bool (*buildSnapshot)(void* ctx, char* out, size_t len);
    void* ctx;
};
