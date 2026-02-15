#pragma once
/**
 * @file WifiRuntime.h
 * @brief Wifi runtime helpers and keys.
 */
#include <string.h>

#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"
#include "Core/Types/IpV4.h"

// RUNTIME_PUBLIC
// Data keys for wifi runtime values.
constexpr DataKey DATAKEY_WIFI_READY = DataKeys::WifiReady;
constexpr DataKey DATAKEY_WIFI_IP = DataKeys::WifiIp;

static inline bool ipEqual(const IpV4& a, const IpV4& b)
{
    return memcmp(a.b, b.b, 4) == 0;
}

static inline bool wifiReady(const DataStore& ds)
{
    return ds.data().wifi.ready;
}

static inline IpV4 wifiIp(const DataStore& ds)
{
    return ds.data().wifi.ip;
}

static inline void setWifiReady(DataStore& ds, bool ready)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.wifi.ready == ready) return;
    rt.wifi.ready = ready;
    ds.notifyChanged(DATAKEY_WIFI_READY, DIRTY_NETWORK);
}

static inline void setWifiIp(DataStore& ds, const IpV4& ip)
{
    RuntimeData& rt = ds.dataMutable();
    if (ipEqual(rt.wifi.ip, ip)) return;
    rt.wifi.ip = ip;
    ds.notifyChanged(DATAKEY_WIFI_IP, DIRTY_NETWORK);
}
