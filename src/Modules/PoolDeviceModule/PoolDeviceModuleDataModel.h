#pragma once
/**
 * @file PoolDeviceModuleDataModel.h
 * @brief Pool device runtime data model contribution.
 */

#include <stdint.h>

constexpr uint8_t POOL_DEVICE_MAX = 8;

enum PoolDeviceRuntimeType : uint8_t {
    POOL_DEVICE_RT_FILTRATION = 0,
    POOL_DEVICE_RT_PERISTALTIC = 1,
    POOL_DEVICE_RT_RELAY_STD = 2
};

enum PoolDeviceRuntimeBlockReason : uint8_t {
    POOL_DEVICE_BLOCK_NONE = 0,
    POOL_DEVICE_BLOCK_DISABLED = 1,
    POOL_DEVICE_BLOCK_INTERLOCK = 2,
    POOL_DEVICE_BLOCK_IO_ERROR = 3
};

struct PoolDeviceRuntimeEntry {
    bool valid = false;
    bool enabled = false;
    bool desiredOn = false;
    bool actualOn = false;
    uint8_t type = POOL_DEVICE_RT_RELAY_STD;
    uint8_t blockReason = POOL_DEVICE_BLOCK_NONE;
    uint32_t runningSecDay = 0;
    uint32_t runningSecMonth = 0;
    uint32_t runningSecYear = 0;
    uint32_t runningSecTotal = 0;
    float injectedMlDay = 0.0f;
    float injectedMlMonth = 0.0f;
    float injectedMlYear = 0.0f;
    float injectedMlTotal = 0.0f;
    float tankRemainingMl = 0.0f;
    uint32_t timestampMs = 0;
};

struct PoolDeviceRuntimeData {
    PoolDeviceRuntimeEntry devices[POOL_DEVICE_MAX];
};

// MODULE_DATA_MODEL: PoolDeviceRuntimeData pool
