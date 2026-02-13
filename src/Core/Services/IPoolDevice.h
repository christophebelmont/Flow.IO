#pragma once
/**
 * @file IPoolDevice.h
 * @brief Pool device domain service interface.
 */
#include <stdint.h>
#include "IIO.h"

/** Result code for PoolDeviceService calls. */
enum PoolDeviceSvcStatus : uint8_t {
    POOLDEV_SVC_OK = 0,
    POOLDEV_SVC_ERR_INVALID_ARG = 1,
    POOLDEV_SVC_ERR_UNKNOWN_SLOT = 2,
    POOLDEV_SVC_ERR_NOT_READY = 3,
    POOLDEV_SVC_ERR_DISABLED = 4,
    POOLDEV_SVC_ERR_INTERLOCK = 5,
    POOLDEV_SVC_ERR_IO = 6
};

/** Static metadata for one pool device slot. */
struct PoolDeviceSvcMeta {
    uint8_t slot = 0;
    uint8_t used = 0;
    uint8_t type = 0;
    uint8_t enabled = 0;
    uint8_t blockReason = 0;
    IoId ioId = IO_ID_INVALID;
    char runtimeId[8] = {0};
    char label[24] = {0};
};

/** Service interface for slot-based pool device control. */
struct PoolDeviceService {
    /** Number of active pool-device slots. */
    uint8_t (*count)(void* ctx);
    /** Metadata lookup for one slot index. */
    PoolDeviceSvcStatus (*meta)(void* ctx, uint8_t slot, PoolDeviceSvcMeta* outMeta);
    /** Read actual hardware state of one slot. */
    PoolDeviceSvcStatus (*readActualOn)(void* ctx, uint8_t slot, uint8_t* outOn, uint32_t* outTsMs);
    /** Write desired state of one slot. */
    PoolDeviceSvcStatus (*writeDesired)(void* ctx, uint8_t slot, uint8_t on);
    /** Refill tracked tank level for one slot (peristaltic pumps). */
    PoolDeviceSvcStatus (*refillTank)(void* ctx, uint8_t slot, float remainingMl);
    /** Opaque implementation context. */
    void* ctx;
};
