#pragma once
/**
 * @file TimeRuntime.h
 * @brief Time runtime helpers and keys.
 */

#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"

// RUNTIME_PUBLIC

// Data keys for time runtime values.
constexpr DataKey DATAKEY_TIME_READY = DataKeys::TimeReady;

static inline bool timeReady(const DataStore& ds)
{
    return ds.data().time.timeReady;
}

static inline void setTimeReady(DataStore& ds, bool ready)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.time.timeReady == ready) return;
    rt.time.timeReady = ready;
    ds.notifyChanged(DATAKEY_TIME_READY, DIRTY_TIME);
}
