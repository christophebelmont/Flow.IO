#pragma once
/**
 * @file PoolDeviceRuntime.h
 * @brief Pool device runtime helpers and keys.
 */

#include <stdint.h>
#include <string.h>
#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"
#include "Modules/PoolDeviceModule/PoolDeviceModuleDataModel.h"

// RUNTIME_PUBLIC

constexpr DataKey DATAKEY_POOL_DEVICE_STATE_BASE = DataKeys::PoolDeviceStateBase;
constexpr DataKey DATAKEY_POOL_DEVICE_METRICS_BASE = DataKeys::PoolDeviceMetricsBase;
static_assert(POOL_DEVICE_MAX <= DataKeys::PoolDeviceStateReservedCount, "DataKeys::PoolDeviceStateReservedCount too small for pool device slots");
static_assert(POOL_DEVICE_MAX <= DataKeys::PoolDeviceMetricsReservedCount, "DataKeys::PoolDeviceMetricsReservedCount too small for pool device slots");

static inline bool poolDeviceRuntimeState(const DataStore& ds, uint8_t idx, PoolDeviceRuntimeStateEntry& out)
{
    if (idx >= POOL_DEVICE_MAX) return false;
    out = ds.data().pool.state[idx];
    return out.valid;
}

static inline bool poolDeviceRuntimeMetrics(const DataStore& ds, uint8_t idx, PoolDeviceRuntimeMetricsEntry& out)
{
    if (idx >= POOL_DEVICE_MAX) return false;
    out = ds.data().pool.metrics[idx];
    return out.valid;
}

static inline bool setPoolDeviceRuntimeState(DataStore& ds, uint8_t idx,
                                             const PoolDeviceRuntimeStateEntry& in,
                                             uint32_t dirtyMask = DIRTY_ACTUATORS)
{
    if (idx >= POOL_DEVICE_MAX) return false;

    RuntimeData& rt = ds.dataMutable();
    PoolDeviceRuntimeStateEntry& cur = rt.pool.state[idx];
    if (memcmp(&cur, &in, sizeof(PoolDeviceRuntimeStateEntry)) == 0) return false;

    cur = in;
    ds.notifyChanged((DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + idx), dirtyMask);
    return true;
}

static inline bool setPoolDeviceRuntimeMetrics(DataStore& ds, uint8_t idx,
                                               const PoolDeviceRuntimeMetricsEntry& in,
                                               uint32_t dirtyMask = DIRTY_SENSORS)
{
    if (idx >= POOL_DEVICE_MAX) return false;

    RuntimeData& rt = ds.dataMutable();
    PoolDeviceRuntimeMetricsEntry& cur = rt.pool.metrics[idx];
    if (memcmp(&cur, &in, sizeof(PoolDeviceRuntimeMetricsEntry)) == 0) return false;

    cur = in;
    ds.notifyChanged((DataKey)(DATAKEY_POOL_DEVICE_METRICS_BASE + idx), dirtyMask);
    return true;
}
