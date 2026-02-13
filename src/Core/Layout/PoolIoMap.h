#pragma once
/**
 * @file PoolIoMap.h
 * @brief Shared static mapping between pool-device slots and IO digital outputs.
 */

#include <stdint.h>
#include "Core/Services/IIO.h"

struct PoolIoBinding {
    /** Stable pool-device slot used by PoolDeviceService/pool.write. */
    uint8_t slot = 0;
    /** IOServiceV2 digital output id bound to this slot. */
    IoId ioId = IO_ID_INVALID;
    /** Home Assistant switch object suffix (without flowioNNN_ prefix). */
    const char* haObjectSuffix = nullptr;
    /** Human-readable name used in HA and setup defaults. */
    const char* name = nullptr;
    /** Optional HA icon. */
    const char* haIcon = nullptr;
};

static constexpr PoolIoBinding FLOW_POOL_IO_BINDINGS[] = {
    {0, (IoId)(IO_ID_DO_BASE + 0), "filtration_pump",   "Filtration Pump",   "mdi:pool"},
    {1, (IoId)(IO_ID_DO_BASE + 1), "ph_pump",           "pH Pump",           "mdi:beaker-outline"},
    {2, (IoId)(IO_ID_DO_BASE + 2), "chlorine_pump",     "Chlorine Pump",     "mdi:water-outline"},
    {5, (IoId)(IO_ID_DO_BASE + 3), "chlorine_generator","Chlorine Generator","mdi:flash"},
    {3, (IoId)(IO_ID_DO_BASE + 4), "robot",             "Robot",             "mdi:robot-vacuum"},
    {6, (IoId)(IO_ID_DO_BASE + 5), "lights",            "Lights",            "mdi:lightbulb"},
    {4, (IoId)(IO_ID_DO_BASE + 6), "fill_pump",         "Fill Pump",         "mdi:water-plus"},
    {7, (IoId)(IO_ID_DO_BASE + 7), "water_heater",      "Water Heater",      "mdi:water-boiler"},
};

static constexpr uint8_t FLOW_POOL_IO_BINDING_COUNT =
    (uint8_t)(sizeof(FLOW_POOL_IO_BINDINGS) / sizeof(FLOW_POOL_IO_BINDINGS[0]));

static inline const PoolIoBinding* flowPoolIoBindingBySlot(uint8_t slot)
{
    for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
        if (FLOW_POOL_IO_BINDINGS[i].slot == slot) return &FLOW_POOL_IO_BINDINGS[i];
    }
    return nullptr;
}

static inline const PoolIoBinding* flowPoolIoBindingByIoId(IoId ioId)
{
    for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
        if (FLOW_POOL_IO_BINDINGS[i].ioId == ioId) return &FLOW_POOL_IO_BINDINGS[i];
    }
    return nullptr;
}
