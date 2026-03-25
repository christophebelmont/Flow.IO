#pragma once

#include <stdint.h>

#include "Core/Services/IIO.h"
#include "Domain/Pool/PoolDomain.h"

struct PoolIoBinding {
    uint8_t slot = 0;
    IoId ioId = IO_ID_INVALID;
    const char* objectSuffix = nullptr;
    const char* name = nullptr;
    const char* haIcon = nullptr;
};

struct PoolSensorBinding {
    uint8_t slot = 0;
    uint8_t kind = 0;
    IoId ioId = IO_ID_INVALID;
    const char* endpointId = nullptr;
    const char* name = nullptr;
    uint8_t runtimeIndex = 0;
};

enum PoolSensorKind : uint8_t {
    POOL_SENSOR_KIND_ANALOG = 0,
    POOL_SENSOR_KIND_DIGITAL = 1
};

namespace PoolBinding {

constexpr uint8_t kDeviceSlotFiltrationPump = 0;
constexpr uint8_t kDeviceSlotPhPump = 1;
constexpr uint8_t kDeviceSlotChlorinePump = 2;
constexpr uint8_t kDeviceSlotRobot = 3;
constexpr uint8_t kDeviceSlotFillPump = 4;
constexpr uint8_t kDeviceSlotChlorineGenerator = 5;
constexpr uint8_t kDeviceSlotLights = 6;
constexpr uint8_t kDeviceSlotWaterHeater = 7;
constexpr uint8_t kDeviceBindingCount = 8;

constexpr uint8_t kSensorSlotOrp = 0;
constexpr uint8_t kSensorSlotPh = 1;
constexpr uint8_t kSensorSlotPsi = 2;
constexpr uint8_t kSensorSlotSpare = 3;
constexpr uint8_t kSensorSlotWaterTemp = 4;
constexpr uint8_t kSensorSlotAirTemp = 5;
constexpr uint8_t kSensorSlotPoolLevel = 6;
constexpr uint8_t kSensorSlotPhLevel = 7;
constexpr uint8_t kSensorSlotChlorineLevel = 8;
constexpr uint8_t kSensorSlotWaterCounter = 9;
constexpr uint8_t kSensorBindingCount = 10;

inline constexpr PoolIoBinding kIoBindings[kDeviceBindingCount] = {
    {PoolDomain::kPoolDevices[0].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[0].legacySlot), PoolDomain::kPoolDevices[0].objectSuffix, PoolDomain::kPoolDevices[0].displayName, PoolDomain::kPoolDevices[0].haIcon},
    {PoolDomain::kPoolDevices[1].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[1].legacySlot), PoolDomain::kPoolDevices[1].objectSuffix, PoolDomain::kPoolDevices[1].displayName, PoolDomain::kPoolDevices[1].haIcon},
    {PoolDomain::kPoolDevices[2].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[2].legacySlot), PoolDomain::kPoolDevices[2].objectSuffix, PoolDomain::kPoolDevices[2].displayName, PoolDomain::kPoolDevices[2].haIcon},
    {PoolDomain::kPoolDevices[3].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[3].legacySlot), PoolDomain::kPoolDevices[3].objectSuffix, PoolDomain::kPoolDevices[3].displayName, PoolDomain::kPoolDevices[3].haIcon},
    {PoolDomain::kPoolDevices[4].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[4].legacySlot), PoolDomain::kPoolDevices[4].objectSuffix, PoolDomain::kPoolDevices[4].displayName, PoolDomain::kPoolDevices[4].haIcon},
    {PoolDomain::kPoolDevices[5].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[5].legacySlot), PoolDomain::kPoolDevices[5].objectSuffix, PoolDomain::kPoolDevices[5].displayName, PoolDomain::kPoolDevices[5].haIcon},
    {PoolDomain::kPoolDevices[6].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[6].legacySlot), PoolDomain::kPoolDevices[6].objectSuffix, PoolDomain::kPoolDevices[6].displayName, PoolDomain::kPoolDevices[6].haIcon},
    {PoolDomain::kPoolDevices[7].legacySlot, (IoId)(IO_ID_DO_BASE + PoolDomain::kPoolDevices[7].legacySlot), PoolDomain::kPoolDevices[7].objectSuffix, PoolDomain::kPoolDevices[7].displayName, PoolDomain::kPoolDevices[7].haIcon},
};

inline constexpr PoolSensorBinding kSensorBindings[kSensorBindingCount] = {
    {PoolDomain::kSensors[0].legacySlot, POOL_SENSOR_KIND_ANALOG, (IoId)(IO_ID_AI_BASE + 0), PoolDomain::kSensors[0].endpointId, PoolDomain::kSensors[0].displayName, PoolDomain::kSensors[0].runtimeIndex},
    {PoolDomain::kSensors[1].legacySlot, POOL_SENSOR_KIND_ANALOG, (IoId)(IO_ID_AI_BASE + 1), PoolDomain::kSensors[1].endpointId, PoolDomain::kSensors[1].displayName, PoolDomain::kSensors[1].runtimeIndex},
    {PoolDomain::kSensors[2].legacySlot, POOL_SENSOR_KIND_ANALOG, (IoId)(IO_ID_AI_BASE + 2), PoolDomain::kSensors[2].endpointId, PoolDomain::kSensors[2].displayName, PoolDomain::kSensors[2].runtimeIndex},
    {PoolDomain::kSensors[3].legacySlot, POOL_SENSOR_KIND_ANALOG, (IoId)(IO_ID_AI_BASE + 3), PoolDomain::kSensors[3].endpointId, PoolDomain::kSensors[3].displayName, PoolDomain::kSensors[3].runtimeIndex},
    {PoolDomain::kSensors[4].legacySlot, POOL_SENSOR_KIND_ANALOG, (IoId)(IO_ID_AI_BASE + 4), PoolDomain::kSensors[4].endpointId, PoolDomain::kSensors[4].displayName, PoolDomain::kSensors[4].runtimeIndex},
    {PoolDomain::kSensors[5].legacySlot, POOL_SENSOR_KIND_ANALOG, (IoId)(IO_ID_AI_BASE + 5), PoolDomain::kSensors[5].endpointId, PoolDomain::kSensors[5].displayName, PoolDomain::kSensors[5].runtimeIndex},
    {PoolDomain::kSensors[6].legacySlot, POOL_SENSOR_KIND_DIGITAL, (IoId)(IO_ID_DI_BASE + 0), PoolDomain::kSensors[6].endpointId, PoolDomain::kSensors[6].displayName, PoolDomain::kSensors[6].runtimeIndex},
    {PoolDomain::kSensors[7].legacySlot, POOL_SENSOR_KIND_DIGITAL, (IoId)(IO_ID_DI_BASE + 1), PoolDomain::kSensors[7].endpointId, PoolDomain::kSensors[7].displayName, PoolDomain::kSensors[7].runtimeIndex},
    {PoolDomain::kSensors[8].legacySlot, POOL_SENSOR_KIND_DIGITAL, (IoId)(IO_ID_DI_BASE + 2), PoolDomain::kSensors[8].endpointId, PoolDomain::kSensors[8].displayName, PoolDomain::kSensors[8].runtimeIndex},
    {PoolDomain::kSensors[9].legacySlot, POOL_SENSOR_KIND_DIGITAL, (IoId)(IO_ID_DI_BASE + 3), PoolDomain::kSensors[9].endpointId, PoolDomain::kSensors[9].displayName, PoolDomain::kSensors[9].runtimeIndex},
};

inline constexpr const PoolIoBinding* ioBindingBySlot(uint8_t slot)
{
    for (uint8_t i = 0; i < kDeviceBindingCount; ++i) {
        if (kIoBindings[i].slot == slot) return &kIoBindings[i];
    }
    return nullptr;
}

inline constexpr const PoolIoBinding* ioBindingByIoId(IoId ioId)
{
    for (uint8_t i = 0; i < kDeviceBindingCount; ++i) {
        if (kIoBindings[i].ioId == ioId) return &kIoBindings[i];
    }
    return nullptr;
}

inline constexpr const PoolSensorBinding* sensorBindingBySlot(uint8_t slot)
{
    for (uint8_t i = 0; i < kSensorBindingCount; ++i) {
        if (kSensorBindings[i].slot == slot) return &kSensorBindings[i];
    }
    return nullptr;
}

inline constexpr const PoolSensorBinding* sensorBindingByIoId(IoId ioId)
{
    for (uint8_t i = 0; i < kSensorBindingCount; ++i) {
        if (kSensorBindings[i].ioId == ioId) return &kSensorBindings[i];
    }
    return nullptr;
}

}  // namespace PoolBinding
