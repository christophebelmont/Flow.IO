#pragma once
/**
 * @file MQTTRuntime.h
 * @brief MQTT runtime helpers and keys.
 */

#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"

// RUNTIME_PUBLIC

// Data keys for MQTT runtime values.
constexpr DataKey DATAKEY_MQTT_READY = DataKeys::MqttReady;
constexpr DataKey DATAKEY_MQTT_RX_DROP = DataKeys::MqttRxDrop;
constexpr DataKey DATAKEY_MQTT_PARSE_FAIL = DataKeys::MqttParseFail;
constexpr DataKey DATAKEY_MQTT_HANDLER_FAIL = DataKeys::MqttHandlerFail;

static inline bool mqttReady(const DataStore& ds)
{
    return ds.data().mqtt.mqttReady;
}

static inline uint32_t mqttRxDrop(const DataStore& ds)
{
    return ds.data().mqtt.rxDrop;
}

static inline uint32_t mqttParseFail(const DataStore& ds)
{
    return ds.data().mqtt.parseFail;
}

static inline uint32_t mqttHandlerFail(const DataStore& ds)
{
    return ds.data().mqtt.handlerFail;
}

static inline void setMqttReady(DataStore& ds, bool ready)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.mqttReady == ready) return;
    rt.mqtt.mqttReady = ready;
    ds.notifyChanged(DATAKEY_MQTT_READY, DIRTY_MQTT);
}

static inline void setMqttRxDrop(DataStore& ds, uint32_t v, bool notify = false)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.rxDrop == v) return;
    rt.mqtt.rxDrop = v;
    if (notify) ds.notifyChanged(DATAKEY_MQTT_RX_DROP, DIRTY_MQTT);
}

static inline void setMqttParseFail(DataStore& ds, uint32_t v, bool notify = false)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.parseFail == v) return;
    rt.mqtt.parseFail = v;
    if (notify) ds.notifyChanged(DATAKEY_MQTT_PARSE_FAIL, DIRTY_MQTT);
}

static inline void setMqttHandlerFail(DataStore& ds, uint32_t v, bool notify = false)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.mqtt.handlerFail == v) return;
    rt.mqtt.handlerFail = v;
    if (notify) ds.notifyChanged(DATAKEY_MQTT_HANDLER_FAIL, DIRTY_MQTT);
}
