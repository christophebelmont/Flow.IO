#pragma once
/**
 * @file IORuntime.h
 * @brief IO runtime helpers and keys.
 */

#include <math.h>
#include <stdint.h>
#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"

// RUNTIME_PUBLIC

constexpr DataKey DATAKEY_IO_BASE = 40;

static inline float ioRoundToPrecision(float value, int32_t decimals)
{
    if (decimals <= 0) return (float)((int32_t)lroundf(value));
    float scale = 1.0f;
    for (int32_t i = 0; i < decimals; ++i) scale *= 10.0f;
    return roundf(value * scale) / scale;
}

static inline bool ioChangedAtPrecision(float a, float b, int32_t decimals)
{
    return ioRoundToPrecision(a, decimals) != ioRoundToPrecision(b, decimals);
}

static inline bool ioEndpointFloat(const DataStore& ds, uint8_t idx, float& out)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;
    const IOEndpointRuntime& ep = ds.data().io.endpoints[idx];
    if (!ep.valid) return false;
    if (ep.valueType != IO_VALUE_FLOAT) return false;
    out = ep.floatValue;
    return true;
}

static inline bool setIoEndpointFloat(DataStore& ds, uint8_t idx, float value, uint32_t tsMs,
                                      uint32_t dirtyMask = DIRTY_SENSORS)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    RuntimeData& rt = ds.dataMutable();
    IOEndpointRuntime& ep = rt.io.endpoints[idx];

    if (ep.valid &&
        ep.valueType == IO_VALUE_FLOAT &&
        ep.floatValue == value &&
        ep.timestampMs == tsMs) {
        return false;
    }

    ep.valid = true;
    ep.valueType = IO_VALUE_FLOAT;
    ep.floatValue = value;
    ep.timestampMs = tsMs;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx), dirtyMask);
    return true;
}

static inline bool setIoEndpointBool(DataStore& ds, uint8_t idx, bool value, uint32_t tsMs,
                                     uint32_t dirtyMask = DIRTY_SENSORS)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    RuntimeData& rt = ds.dataMutable();
    IOEndpointRuntime& ep = rt.io.endpoints[idx];

    if (ep.valid &&
        ep.valueType == IO_VALUE_BOOL &&
        ep.boolValue == value &&
        ep.timestampMs == tsMs) {
        return false;
    }

    ep.valid = true;
    ep.valueType = IO_VALUE_BOOL;
    ep.boolValue = value;
    ep.timestampMs = tsMs;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx), dirtyMask);
    return true;
}

static inline bool setIoEndpointInt(DataStore& ds, uint8_t idx, int32_t value, uint32_t tsMs,
                                    uint32_t dirtyMask = DIRTY_SENSORS)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    RuntimeData& rt = ds.dataMutable();
    IOEndpointRuntime& ep = rt.io.endpoints[idx];

    if (ep.valid &&
        ep.valueType == IO_VALUE_INT32 &&
        ep.intValue == value &&
        ep.timestampMs == tsMs) {
        return false;
    }

    ep.valid = true;
    ep.valueType = IO_VALUE_INT32;
    ep.intValue = value;
    ep.timestampMs = tsMs;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx), dirtyMask);
    return true;
}
