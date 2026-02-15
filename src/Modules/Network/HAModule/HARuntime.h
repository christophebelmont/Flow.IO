#pragma once
/**
 * @file HARuntime.h
 * @brief Home Assistant runtime helpers and keys.
 */

#include <string.h>

#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"

// RUNTIME_PUBLIC

constexpr DataKey DATAKEY_HA_PUBLISHED = DataKeys::HaPublished;
constexpr DataKey DATAKEY_HA_VENDOR = DataKeys::HaVendor;
constexpr DataKey DATAKEY_HA_DEVICE_ID = DataKeys::HaDeviceId;

static inline bool haAutoconfigPublished(const DataStore& ds)
{
    return ds.data().ha.autoconfigPublished;
}

static inline const char* haVendor(const DataStore& ds)
{
    return ds.data().ha.vendor;
}

static inline const char* haDeviceId(const DataStore& ds)
{
    return ds.data().ha.deviceId;
}

static inline void setHaAutoconfigPublished(DataStore& ds, bool published)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.ha.autoconfigPublished == published) return;
    rt.ha.autoconfigPublished = published;
    ds.notifyChanged(DATAKEY_HA_PUBLISHED, DIRTY_MQTT);
}

static inline void setHaVendor(DataStore& ds, const char* vendor)
{
    if (!vendor) return;
    RuntimeData& rt = ds.dataMutable();
    if (strncmp(rt.ha.vendor, vendor, sizeof(rt.ha.vendor)) == 0) return;
    strncpy(rt.ha.vendor, vendor, sizeof(rt.ha.vendor) - 1);
    rt.ha.vendor[sizeof(rt.ha.vendor) - 1] = '\0';
    ds.notifyChanged(DATAKEY_HA_VENDOR, DIRTY_MQTT);
}

static inline void setHaDeviceId(DataStore& ds, const char* deviceId)
{
    if (!deviceId) return;
    RuntimeData& rt = ds.dataMutable();
    if (strncmp(rt.ha.deviceId, deviceId, sizeof(rt.ha.deviceId)) == 0) return;
    strncpy(rt.ha.deviceId, deviceId, sizeof(rt.ha.deviceId) - 1);
    rt.ha.deviceId[sizeof(rt.ha.deviceId) - 1] = '\0';
    ds.notifyChanged(DATAKEY_HA_DEVICE_ID, DIRTY_MQTT);
}
