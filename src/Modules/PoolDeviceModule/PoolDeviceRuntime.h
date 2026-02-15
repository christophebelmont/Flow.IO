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

constexpr DataKey DATAKEY_POOL_DEVICE_BASE = DataKeys::PoolDeviceBase;
static_assert(POOL_DEVICE_MAX <= DataKeys::PoolDeviceReservedCount, "DataKeys::PoolDeviceReservedCount too small for pool device slots");

static inline bool poolDeviceRuntime(const DataStore& ds, uint8_t idx, PoolDeviceRuntimeEntry& out)
{
    if (idx >= POOL_DEVICE_MAX) return false;
    out = ds.data().pool.devices[idx];
    return out.valid;
}

static inline bool setPoolDeviceRuntime(DataStore& ds, uint8_t idx,
                                        const PoolDeviceRuntimeEntry& in,
                                        uint32_t dirtyMask = DIRTY_SENSORS)
{
    if (idx >= POOL_DEVICE_MAX) return false;

    RuntimeData& rt = ds.dataMutable();
    PoolDeviceRuntimeEntry& cur = rt.pool.devices[idx];
    if (memcmp(&cur, &in, sizeof(PoolDeviceRuntimeEntry)) == 0) return false;

    cur = in;
    ds.notifyChanged((DataKey)(DATAKEY_POOL_DEVICE_BASE + idx), dirtyMask);
    return true;
}
