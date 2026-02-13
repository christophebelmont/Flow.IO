#pragma once
/**
 * @file PoolSensorMap.h
 * @brief Shared static mapping between pool sensor slots and IO ids.
 */

#include <stdint.h>
#include "Core/Services/IIO.h"

enum PoolSensorKind : uint8_t {
    POOL_SENSOR_KIND_ANALOG = 0,
    POOL_SENSOR_KIND_DIGITAL = 1
};

enum PoolSensorSlot : uint8_t {
    POOL_SENSOR_SLOT_ORP = 0,
    POOL_SENSOR_SLOT_PH = 1,
    POOL_SENSOR_SLOT_PSI = 2,
    POOL_SENSOR_SLOT_SPARE = 3,
    POOL_SENSOR_SLOT_WATER_TEMP = 4,
    POOL_SENSOR_SLOT_AIR_TEMP = 5,
    POOL_SENSOR_SLOT_POOL_LEVEL = 6
};

struct PoolSensorBinding {
    /** Stable sensor slot used by business modules and setup defaults. */
    uint8_t slot = 0;
    /** IO endpoint kind associated with this slot. */
    uint8_t kind = POOL_SENSOR_KIND_ANALOG;
    /** IOServiceV2 id bound to this sensor slot. */
    IoId ioId = IO_ID_INVALID;
    /** Sensor id used by IOModule endpoint definitions. */
    const char* endpointId = nullptr;
    /** Human-readable sensor name. */
    const char* name = nullptr;
    /** DataStore runtime index used by setIoEndpointFloat/setIoEndpointBool. */
    uint8_t runtimeIndex = 0;
};

static constexpr PoolSensorBinding FLOW_POOL_SENSOR_BINDINGS[] = {
    {POOL_SENSOR_SLOT_ORP,        POOL_SENSOR_KIND_ANALOG,  (IoId)(IO_ID_AI_BASE + 0), "ORP",               "ORP",               0},
    {POOL_SENSOR_SLOT_PH,         POOL_SENSOR_KIND_ANALOG,  (IoId)(IO_ID_AI_BASE + 1), "pH",                "pH",                1},
    {POOL_SENSOR_SLOT_PSI,        POOL_SENSOR_KIND_ANALOG,  (IoId)(IO_ID_AI_BASE + 2), "PSI",               "PSI",               2},
    {POOL_SENSOR_SLOT_SPARE,      POOL_SENSOR_KIND_ANALOG,  (IoId)(IO_ID_AI_BASE + 3), "Spare",             "Spare",             3},
    {POOL_SENSOR_SLOT_WATER_TEMP, POOL_SENSOR_KIND_ANALOG,  (IoId)(IO_ID_AI_BASE + 4), "Water Temperature", "Water Temperature", 4},
    {POOL_SENSOR_SLOT_AIR_TEMP,   POOL_SENSOR_KIND_ANALOG,  (IoId)(IO_ID_AI_BASE + 5), "Air Temperature",   "Air Temperature",   5},
    {POOL_SENSOR_SLOT_POOL_LEVEL, POOL_SENSOR_KIND_DIGITAL, (IoId)(IO_ID_DI_BASE + 0), "Pool Level",        "Pool Level",        20},
};

static constexpr uint8_t FLOW_POOL_SENSOR_BINDING_COUNT =
    (uint8_t)(sizeof(FLOW_POOL_SENSOR_BINDINGS) / sizeof(FLOW_POOL_SENSOR_BINDINGS[0]));

static inline const PoolSensorBinding* flowPoolSensorBySlot(uint8_t slot)
{
    for (uint8_t i = 0; i < FLOW_POOL_SENSOR_BINDING_COUNT; ++i) {
        if (FLOW_POOL_SENSOR_BINDINGS[i].slot == slot) return &FLOW_POOL_SENSOR_BINDINGS[i];
    }
    return nullptr;
}

static inline const PoolSensorBinding* flowPoolSensorByIoId(IoId ioId)
{
    for (uint8_t i = 0; i < FLOW_POOL_SENSOR_BINDING_COUNT; ++i) {
        if (FLOW_POOL_SENSOR_BINDINGS[i].ioId == ioId) return &FLOW_POOL_SENSOR_BINDINGS[i];
    }
    return nullptr;
}
