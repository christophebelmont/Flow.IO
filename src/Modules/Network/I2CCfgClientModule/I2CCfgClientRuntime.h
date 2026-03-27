#pragma once
/**
 * @file I2CCfgClientRuntime.h
 * @brief DataStore helpers for mirrored Flow runtime values.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Core/DataKeys.h"
#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"

// RUNTIME_PUBLIC

constexpr DataKey DATAKEY_FLOW_REMOTE_READY = DataKeys::FlowRemoteReady;
constexpr DataKey DATAKEY_FLOW_REMOTE_LINK_OK = DataKeys::FlowRemoteLinkOk;
constexpr DataKey DATAKEY_FLOW_REMOTE_FIRMWARE = DataKeys::FlowRemoteFirmware;
constexpr DataKey DATAKEY_FLOW_REMOTE_HAS_RSSI = DataKeys::FlowRemoteHasRssi;
constexpr DataKey DATAKEY_FLOW_REMOTE_RSSI_DBM = DataKeys::FlowRemoteRssiDbm;
constexpr DataKey DATAKEY_FLOW_REMOTE_HAS_HEAP_FRAG = DataKeys::FlowRemoteHasHeapFrag;
constexpr DataKey DATAKEY_FLOW_REMOTE_HEAP_FRAG_PCT = DataKeys::FlowRemoteHeapFragPct;
constexpr DataKey DATAKEY_FLOW_REMOTE_MQTT_READY = DataKeys::FlowRemoteMqttReady;
constexpr DataKey DATAKEY_FLOW_REMOTE_MQTT_RX_DROP = DataKeys::FlowRemoteMqttRxDrop;
constexpr DataKey DATAKEY_FLOW_REMOTE_MQTT_PARSE_FAIL = DataKeys::FlowRemoteMqttParseFail;
constexpr DataKey DATAKEY_FLOW_REMOTE_I2C_REQ_COUNT = DataKeys::FlowRemoteI2cReqCount;
constexpr DataKey DATAKEY_FLOW_REMOTE_I2C_BAD_REQ_COUNT = DataKeys::FlowRemoteI2cBadReqCount;
constexpr DataKey DATAKEY_FLOW_REMOTE_I2C_LAST_REQ_AGO_MS = DataKeys::FlowRemoteI2cLastReqAgoMs;
constexpr DataKey DATAKEY_FLOW_REMOTE_HAS_POOL_MODES = DataKeys::FlowRemoteHasPoolModes;
constexpr DataKey DATAKEY_FLOW_REMOTE_FILTRATION_AUTO = DataKeys::FlowRemoteFiltrationAuto;
constexpr DataKey DATAKEY_FLOW_REMOTE_WINTER_MODE = DataKeys::FlowRemoteWinterMode;
constexpr DataKey DATAKEY_FLOW_REMOTE_PH_AUTO_MODE = DataKeys::FlowRemotePhAutoMode;
constexpr DataKey DATAKEY_FLOW_REMOTE_ORP_AUTO_MODE = DataKeys::FlowRemoteOrpAutoMode;
constexpr DataKey DATAKEY_FLOW_REMOTE_FILTRATION_ON = DataKeys::FlowRemoteFiltrationOn;
constexpr DataKey DATAKEY_FLOW_REMOTE_PH_PUMP_ON = DataKeys::FlowRemotePhPumpOn;
constexpr DataKey DATAKEY_FLOW_REMOTE_CHLORINE_PUMP_ON = DataKeys::FlowRemoteChlorinePumpOn;
constexpr DataKey DATAKEY_FLOW_REMOTE_HAS_PH = DataKeys::FlowRemoteHasPh;
constexpr DataKey DATAKEY_FLOW_REMOTE_PH_VALUE = DataKeys::FlowRemotePhValue;
constexpr DataKey DATAKEY_FLOW_REMOTE_HAS_ORP = DataKeys::FlowRemoteHasOrp;
constexpr DataKey DATAKEY_FLOW_REMOTE_ORP_VALUE = DataKeys::FlowRemoteOrpValue;
constexpr DataKey DATAKEY_FLOW_REMOTE_HAS_WATER_TEMP = DataKeys::FlowRemoteHasWaterTemp;
constexpr DataKey DATAKEY_FLOW_REMOTE_WATER_TEMP = DataKeys::FlowRemoteWaterTemp;
constexpr DataKey DATAKEY_FLOW_REMOTE_HAS_AIR_TEMP = DataKeys::FlowRemoteHasAirTemp;
constexpr DataKey DATAKEY_FLOW_REMOTE_AIR_TEMP = DataKeys::FlowRemoteAirTemp;

static inline const FlowRemoteRuntimeData& flowRemoteRuntime(const DataStore& ds)
{
    return ds.data().flowRemote;
}

static inline bool flowRemoteCopyText_(char* out, size_t outLen, const char* in)
{
    if (!out || outLen == 0) return false;
    if (!in) in = "";
    if (strncmp(out, in, outLen) == 0) return false;
    snprintf(out, outLen, "%s", in);
    return true;
}

static inline bool setFlowRemoteReady(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.ready == v) return false;
    rt.flowRemote.ready = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_READY);
    return true;
}

static inline bool setFlowRemoteLinkOk(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.linkOk == v) return false;
    rt.flowRemote.linkOk = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_LINK_OK);
    return true;
}

static inline bool setFlowRemoteFirmware(DataStore& ds, const char* v)
{
    RuntimeData& rt = ds.dataMutable();
    if (!flowRemoteCopyText_(rt.flowRemote.firmware, sizeof(rt.flowRemote.firmware), v)) return false;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_FIRMWARE);
    return true;
}

static inline bool setFlowRemoteHasRssi(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.hasRssi == v) return false;
    rt.flowRemote.hasRssi = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HAS_RSSI);
    return true;
}

static inline bool setFlowRemoteRssiDbm(DataStore& ds, int32_t v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.rssiDbm == v) return false;
    rt.flowRemote.rssiDbm = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_RSSI_DBM);
    return true;
}

static inline bool setFlowRemoteHasHeapFrag(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.hasHeapFrag == v) return false;
    rt.flowRemote.hasHeapFrag = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HAS_HEAP_FRAG);
    return true;
}

static inline bool setFlowRemoteHeapFragPct(DataStore& ds, uint8_t v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.heapFragPct == v) return false;
    rt.flowRemote.heapFragPct = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HEAP_FRAG_PCT);
    return true;
}

static inline bool setFlowRemoteMqttReady(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.mqttReady == v) return false;
    rt.flowRemote.mqttReady = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_MQTT_READY);
    return true;
}

static inline bool setFlowRemoteMqttRxDrop(DataStore& ds, uint32_t v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.mqttRxDrop == v) return false;
    rt.flowRemote.mqttRxDrop = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_MQTT_RX_DROP);
    return true;
}

static inline bool setFlowRemoteMqttParseFail(DataStore& ds, uint32_t v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.mqttParseFail == v) return false;
    rt.flowRemote.mqttParseFail = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_MQTT_PARSE_FAIL);
    return true;
}

static inline bool setFlowRemoteI2cReqCount(DataStore& ds, uint32_t v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.i2cReqCount == v) return false;
    rt.flowRemote.i2cReqCount = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_I2C_REQ_COUNT);
    return true;
}

static inline bool setFlowRemoteI2cBadReqCount(DataStore& ds, uint32_t v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.i2cBadReqCount == v) return false;
    rt.flowRemote.i2cBadReqCount = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_I2C_BAD_REQ_COUNT);
    return true;
}

static inline bool setFlowRemoteI2cLastReqAgoMs(DataStore& ds, uint32_t v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.i2cLastReqAgoMs == v) return false;
    rt.flowRemote.i2cLastReqAgoMs = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_I2C_LAST_REQ_AGO_MS);
    return true;
}

static inline bool setFlowRemoteHasPoolModes(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.hasPoolModes == v) return false;
    rt.flowRemote.hasPoolModes = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HAS_POOL_MODES);
    return true;
}

static inline bool setFlowRemoteFiltrationAuto(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.filtrationAuto == v) return false;
    rt.flowRemote.filtrationAuto = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_FILTRATION_AUTO);
    return true;
}

static inline bool setFlowRemoteWinterMode(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.winterMode == v) return false;
    rt.flowRemote.winterMode = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_WINTER_MODE);
    return true;
}

static inline bool setFlowRemotePhAutoMode(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.phAutoMode == v) return false;
    rt.flowRemote.phAutoMode = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_PH_AUTO_MODE);
    return true;
}

static inline bool setFlowRemoteOrpAutoMode(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.orpAutoMode == v) return false;
    rt.flowRemote.orpAutoMode = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_ORP_AUTO_MODE);
    return true;
}

static inline bool setFlowRemoteFiltrationOn(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.filtrationOn == v) return false;
    rt.flowRemote.filtrationOn = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_FILTRATION_ON);
    return true;
}

static inline bool setFlowRemotePhPumpOn(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.phPumpOn == v) return false;
    rt.flowRemote.phPumpOn = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_PH_PUMP_ON);
    return true;
}

static inline bool setFlowRemoteChlorinePumpOn(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.chlorinePumpOn == v) return false;
    rt.flowRemote.chlorinePumpOn = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_CHLORINE_PUMP_ON);
    return true;
}

static inline bool setFlowRemoteHasPh(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.hasPh == v) return false;
    rt.flowRemote.hasPh = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HAS_PH);
    return true;
}

static inline bool setFlowRemotePhValue(DataStore& ds, float v)
{
    RuntimeData& rt = ds.dataMutable();
    if (fabsf(rt.flowRemote.phValue - v) <= 0.005f) return false;
    rt.flowRemote.phValue = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_PH_VALUE);
    return true;
}

static inline bool setFlowRemoteHasOrp(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.hasOrp == v) return false;
    rt.flowRemote.hasOrp = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HAS_ORP);
    return true;
}

static inline bool setFlowRemoteOrpValue(DataStore& ds, float v)
{
    RuntimeData& rt = ds.dataMutable();
    if (fabsf(rt.flowRemote.orpValue - v) <= 0.5f) return false;
    rt.flowRemote.orpValue = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_ORP_VALUE);
    return true;
}

static inline bool setFlowRemoteHasWaterTemp(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.hasWaterTemp == v) return false;
    rt.flowRemote.hasWaterTemp = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HAS_WATER_TEMP);
    return true;
}

static inline bool setFlowRemoteWaterTemp(DataStore& ds, float v)
{
    RuntimeData& rt = ds.dataMutable();
    if (fabsf(rt.flowRemote.waterTemp - v) <= 0.05f) return false;
    rt.flowRemote.waterTemp = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_WATER_TEMP);
    return true;
}

static inline bool setFlowRemoteHasAirTemp(DataStore& ds, bool v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.flowRemote.hasAirTemp == v) return false;
    rt.flowRemote.hasAirTemp = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_HAS_AIR_TEMP);
    return true;
}

static inline bool setFlowRemoteAirTemp(DataStore& ds, float v)
{
    RuntimeData& rt = ds.dataMutable();
    if (fabsf(rt.flowRemote.airTemp - v) <= 0.05f) return false;
    rt.flowRemote.airTemp = v;
    ds.notifyChanged(DATAKEY_FLOW_REMOTE_AIR_TEMP);
    return true;
}

static inline void applyFlowRemoteRuntimeSnapshot(DataStore& ds, const FlowRemoteRuntimeData& in)
{
    setFlowRemoteReady(ds, in.ready);
    setFlowRemoteLinkOk(ds, in.linkOk);
    setFlowRemoteFirmware(ds, in.firmware);
    setFlowRemoteHasRssi(ds, in.hasRssi);
    setFlowRemoteRssiDbm(ds, in.rssiDbm);
    setFlowRemoteHasHeapFrag(ds, in.hasHeapFrag);
    setFlowRemoteHeapFragPct(ds, in.heapFragPct);
    setFlowRemoteMqttReady(ds, in.mqttReady);
    setFlowRemoteMqttRxDrop(ds, in.mqttRxDrop);
    setFlowRemoteMqttParseFail(ds, in.mqttParseFail);
    setFlowRemoteI2cReqCount(ds, in.i2cReqCount);
    setFlowRemoteI2cBadReqCount(ds, in.i2cBadReqCount);
    setFlowRemoteI2cLastReqAgoMs(ds, in.i2cLastReqAgoMs);
    setFlowRemoteHasPoolModes(ds, in.hasPoolModes);
    setFlowRemoteFiltrationAuto(ds, in.filtrationAuto);
    setFlowRemoteWinterMode(ds, in.winterMode);
    setFlowRemotePhAutoMode(ds, in.phAutoMode);
    setFlowRemoteOrpAutoMode(ds, in.orpAutoMode);
    setFlowRemoteFiltrationOn(ds, in.filtrationOn);
    setFlowRemotePhPumpOn(ds, in.phPumpOn);
    setFlowRemoteChlorinePumpOn(ds, in.chlorinePumpOn);
    setFlowRemoteHasPh(ds, in.hasPh);
    setFlowRemotePhValue(ds, in.phValue);
    setFlowRemoteHasOrp(ds, in.hasOrp);
    setFlowRemoteOrpValue(ds, in.orpValue);
    setFlowRemoteHasWaterTemp(ds, in.hasWaterTemp);
    setFlowRemoteWaterTemp(ds, in.waterTemp);
    setFlowRemoteHasAirTemp(ds, in.hasAirTemp);
    setFlowRemoteAirTemp(ds, in.airTemp);
}
