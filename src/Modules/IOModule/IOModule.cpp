/**
 * @file IOModule.cpp
 * @brief Implementation file.
 */

#include "IOModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::IOModule)
#include "Core/ModuleLog.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/IOModule/IORuntime.h"
#include <Arduino.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace {
static constexpr uint8_t kIoCfgProducerId = 47;
static constexpr uint8_t kCfgBranchIo = 1;
static constexpr uint8_t kCfgBranchIoDebug = 2;
static constexpr uint8_t kCfgBranchIoA0 = 3;
static constexpr uint8_t kCfgBranchIoA1 = 4;
static constexpr uint8_t kCfgBranchIoA2 = 5;
static constexpr uint8_t kCfgBranchIoA3 = 6;
static constexpr uint8_t kCfgBranchIoA4 = 7;
static constexpr uint8_t kCfgBranchIoA5 = 8;
static constexpr uint8_t kCfgBranchIoD0 = 9;
static constexpr uint8_t kCfgBranchIoD1 = 10;
static constexpr uint8_t kCfgBranchIoD2 = 11;
static constexpr uint8_t kCfgBranchIoD3 = 12;
static constexpr uint8_t kCfgBranchIoD4 = 13;
static constexpr uint8_t kCfgBranchIoD5 = 14;
static constexpr uint8_t kCfgBranchIoD6 = 15;
static constexpr uint8_t kCfgBranchIoD7 = 16;

static constexpr uint8_t kCfgBranchIoI0 = 17;
static constexpr uint8_t kCfgBranchIoI1 = 18;
static constexpr uint8_t kCfgBranchIoI2 = 19;
static constexpr uint8_t kCfgBranchIoI3 = 20;
static constexpr uint8_t kCfgBranchIoI4 = 21;
static constexpr MqttConfigRouteProducer::Route kIoCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Io, kCfgBranchIo}, "io", "io", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {2, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoDebug}, "io/debug", "io/debug", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {3, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoA0}, "io/input/a0", "io/input/a0", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {4, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoA1}, "io/input/a1", "io/input/a1", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {5, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoA2}, "io/input/a2", "io/input/a2", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {6, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoA3}, "io/input/a3", "io/input/a3", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {7, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoA4}, "io/input/a4", "io/input/a4", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {8, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoA5}, "io/input/a5", "io/input/a5", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {9, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD0}, "io/output/d0", "io/output/d0", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {10, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD1}, "io/output/d1", "io/output/d1", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {11, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD2}, "io/output/d2", "io/output/d2", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {12, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD3}, "io/output/d3", "io/output/d3", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {13, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD4}, "io/output/d4", "io/output/d4", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {14, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD5}, "io/output/d5", "io/output/d5", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {15, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD6}, "io/output/d6", "io/output/d6", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {16, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoD7}, "io/output/d7", "io/output/d7", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {17, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI0}, "io/input/i0", "io/input/i0", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {18, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI1}, "io/input/i1", "io/input/i1", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {19, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI2}, "io/input/i2", "io/input/i2", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {20, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI3}, "io/input/i3", "io/input/i3", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {21, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI4}, "io/input/i4", "io/input/i4", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
}

static bool hasDecimalSuffixLocal(const char* p)
{
    if (!p || *p == '\0') return false;
    while (*p) {
        if (*p < '0' || *p > '9') return false;
        ++p;
    }
    return true;
}

static bool isInputEndpointIdLocal(const char* id)
{
    if (!id || id[0] == '\0') return false;
    if ((id[0] == 'a' || id[0] == 'i') && hasDecimalSuffixLocal(id + 1)) return true;
    return false;
}

static bool isOutputEndpointIdLocal(const char* id)
{
    if (!id || id[0] == '\0') return false;
    if (id[0] == 'd' && hasDecimalSuffixLocal(id + 1)) return true;
    return strcmp(id, "status_leds_mask") == 0;
}

void IOModule::setOneWireBuses(OneWireBus* water, OneWireBus* air)
{
    oneWireWater_ = water;
    oneWireAir_ = air;
}

void IOModule::setBindingPorts(const IOBindingPortSpec* ports, uint8_t count)
{
    bindingPorts_ = ports;
    bindingPortCount_ = count;
}

bool IOModule::defineAnalogInput(const IOAnalogDefinition& def)
{
    if (def.id[0] == '\0') return false;
    if (def.ioId == IO_ID_INVALID) return false;
    if (def.ioId < IO_ID_AI_BASE || def.ioId >= IO_ID_AI_MAX) return false;

    const uint8_t analogIdx = (uint8_t)(def.ioId - IO_ID_AI_BASE);
    if (analogSlots_[analogIdx].used) return false;

    AnalogSlot& slot = analogSlots_[analogIdx];
    slot.used = true;
    slot.ioId = def.ioId;
    slot.def = def;
    slot.def.ioId = slot.ioId;

    if (analogIdx < ANALOG_CFG_SLOTS) {
        strncpy(analogCfg_[analogIdx].name, def.id, sizeof(analogCfg_[analogIdx].name) - 1);
        analogCfg_[analogIdx].name[sizeof(analogCfg_[analogIdx].name) - 1] = '\0';
        analogCfg_[analogIdx].bindingPort = def.bindingPort;
        analogCfg_[analogIdx].c0 = def.c0;
        analogCfg_[analogIdx].c1 = def.c1;
        analogCfg_[analogIdx].precision = def.precision;
    }

    return true;
}

bool IOModule::digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.kind != kind) continue;
        if (s.logicalIdx != logicalIdx) continue;
        return true;
    }
    return false;
}

bool IOModule::findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.kind != kind) continue;
        if (s.logicalIdx != logicalIdx) continue;
        slotIdxOut = i;
        return true;
    }
    return false;
}

bool IOModule::findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.ioId != id) continue;
        slotIdxOut = i;
        return true;
    }
    return false;
}

void IOModule::beginIoCycle_(uint32_t nowMs)
{
    ++lastCycle_.seq;
    lastCycle_.tsMs = nowMs;
    lastCycle_.changedCount = 0;
}

void IOModule::markIoCycleChanged_(IoId id)
{
    if (id == IO_ID_INVALID) return;

    for (uint8_t i = 0; i < lastCycle_.changedCount; ++i) {
        if (lastCycle_.changedIds[i] == id) return;
    }

    if (lastCycle_.changedCount >= IO_MAX_CHANGED_IDS) return;
    lastCycle_.changedIds[lastCycle_.changedCount++] = id;
}

bool IOModule::defineDigitalInput(const IODigitalInputDefinition& def)
{
    if (def.id[0] == '\0') return false;
    if (def.bindingPort == IO_PORT_INVALID) return false;
    if (def.ioId == IO_ID_INVALID) return false;
    if (def.ioId < IO_ID_DI_BASE || def.ioId >= IO_ID_DI_MAX) return false;

    const uint8_t logicalIdx = (uint8_t)(def.ioId - IO_ID_DI_BASE);
    if (digitalLogicalUsed_(DIGITAL_SLOT_INPUT, logicalIdx)) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (s.used) continue;
        s.used = true;
        s.ioId = def.ioId;
        s.kind = DIGITAL_SLOT_INPUT;
        s.logicalIdx = logicalIdx;
        s.inDef = def;
        s.inDef.ioId = s.ioId;
        s.owner = this;
        if (logicalIdx < MAX_DIGITAL_INPUTS) {
            strncpy(digitalInCfg_[logicalIdx].name, def.id, sizeof(digitalInCfg_[logicalIdx].name) - 1);
            digitalInCfg_[logicalIdx].name[sizeof(digitalInCfg_[logicalIdx].name) - 1] = '\0';
            digitalInCfg_[logicalIdx].bindingPort = def.bindingPort;
            digitalInCfg_[logicalIdx].activeHigh = def.activeHigh;
            digitalInCfg_[logicalIdx].pullMode = def.pullMode;
            digitalInCfg_[logicalIdx].mode = def.mode;
            digitalInCfg_[logicalIdx].edgeMode = def.edgeMode;
            digitalInCfg_[logicalIdx].counterDebounceUs = def.counterDebounceUs;
        }
        return true;
    }

    return false;
}

bool IOModule::defineDigitalOutput(const IODigitalOutputDefinition& def)
{
    if (def.id[0] == '\0') return false;
    if (def.bindingPort == IO_PORT_INVALID) return false;
    if (def.ioId == IO_ID_INVALID) return false;
    if (def.ioId < IO_ID_DO_BASE || def.ioId >= IO_ID_DO_MAX) return false;

    const uint8_t logicalIdx = (uint8_t)(def.ioId - IO_ID_DO_BASE);
    if (digitalLogicalUsed_(DIGITAL_SLOT_OUTPUT, logicalIdx)) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (s.used) continue;
        s.used = true;
        s.ioId = def.ioId;
        s.kind = DIGITAL_SLOT_OUTPUT;
        s.logicalIdx = logicalIdx;
        s.outDef = def;
        s.outDef.ioId = s.ioId;
        s.owner = this;

        if (logicalIdx < DIGITAL_CFG_SLOTS) {
            const uint8_t cfgIdx = logicalIdx;
            strncpy(digitalCfg_[cfgIdx].name, def.id, sizeof(digitalCfg_[cfgIdx].name) - 1);
            digitalCfg_[cfgIdx].name[sizeof(digitalCfg_[cfgIdx].name) - 1] = '\0';
            digitalCfg_[cfgIdx].bindingPort = def.bindingPort;
            digitalCfg_[cfgIdx].activeHigh = def.activeHigh;
            digitalCfg_[cfgIdx].initialOn = def.initialOn;
            digitalCfg_[cfgIdx].momentary = def.momentary;
            digitalCfg_[cfgIdx].pulseMs = (int32_t)def.pulseMs;
        }
        return true;
    }

    return false;
}

const char* IOModule::analogSlotName(uint8_t idx) const
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return nullptr;
    if (!analogSlots_[idx].used) return nullptr;
    if (analogSlots_[idx].def.id[0] == '\0') return nullptr;
    return analogSlots_[idx].def.id;
}

bool IOModule::analogSlotUsed(uint8_t idx) const
{
    return idx < MAX_ANALOG_ENDPOINTS && analogSlots_[idx].used;
}

bool IOModule::digitalInputSlotUsed(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    return logicalIdx < MAX_DIGITAL_INPUTS && findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx);
}

uint8_t IOModule::digitalInputValueType(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return IO_VAL_BOOL;
    if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx)) return IO_VAL_BOOL;
    const DigitalSlot& s = digitalSlots_[slotIdx];
    return (s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_VAL_INT32 : IO_VAL_BOOL;
}

bool IOModule::digitalOutputSlotUsed(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    return logicalIdx < MAX_DIGITAL_OUTPUTS && findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logicalIdx, slotIdx);
}

int32_t IOModule::analogPrecision(uint8_t idx) const
{
    if (idx >= ANALOG_CFG_SLOTS) return 0;
    return sanitizeAnalogPrecision_(analogCfg_[idx].precision);
}

uint16_t IOModule::takeAnalogConfigDirtyMask()
{
    const uint16_t mask = analogConfigDirtyMask_;
    analogConfigDirtyMask_ = 0;
    return mask;
}

const char* IOModule::endpointLabel(const char* endpointId) const
{
    if (!endpointId || endpointId[0] == '\0') return nullptr;
    if (endpointId[0] == 'a' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        if (idx < ANALOG_CFG_SLOTS && analogCfg_[idx].name[0] != '\0') return analogCfg_[idx].name;
    }
    if (endpointId[0] == 'i' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, idx, slotIdx)) {
            const DigitalSlot& s = digitalSlots_[slotIdx];
            if (idx < MAX_DIGITAL_INPUTS && digitalInCfg_[idx].name[0] != '\0') return digitalInCfg_[idx].name;
            if (s.inDef.id[0] != '\0') return s.inDef.id;
        }
    }
    if (endpointId[0] == 'd' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        if (idx < DIGITAL_CFG_SLOTS && digitalCfg_[idx].name[0] != '\0') return digitalCfg_[idx].name;
    }
    return nullptr;
}

bool IOModule::buildInputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const
{
    return buildGroupSnapshot_(out, len, true, maxTsOut);
}

bool IOModule::buildOutputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const
{
    return buildGroupSnapshot_(out, len, false, maxTsOut);
}

bool IOModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    if (!dataStore_) return writer.writeUnavailable(makeRuntimeUiId(moduleId(), valueId));

    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
    uint8_t runtimeIndex = 0xFF;

    switch (valueId) {
        case RuntimeUiWaterTemp:
            runtimeIndex = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotWaterTemp].runtimeIndex;
            break;
        case RuntimeUiAirTemp:
            runtimeIndex = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotAirTemp].runtimeIndex;
            break;
        case RuntimeUiPh:
            runtimeIndex = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPh].runtimeIndex;
            break;
        case RuntimeUiOrp:
            runtimeIndex = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotOrp].runtimeIndex;
            break;
        case RuntimeUiWaterCounter: {
            runtimeIndex = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotWaterCounter].runtimeIndex;
            int32_t value = 0;
            if (!ioEndpointInt(*dataStore_, runtimeIndex, value)) {
                return writer.writeUnavailable(runtimeId);
            }
            return writer.writeI32(runtimeId, value);
        }
        default:
            return false;
    }

    float value = 0.0f;
    if (!ioEndpointFloat(*dataStore_, runtimeIndex, value)) {
        return writer.writeUnavailable(runtimeId);
    }
    return writer.writeF32(runtimeId, value);
}

uint8_t IOModule::runtimeSnapshotCount() const
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (analogSlots_[i].used) ++count;
    }
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (digitalSlots_[i].used) ++count;
    }
    return count;
}

bool IOModule::runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& routeTypeOut, uint8_t& slotIdxOut) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    uint8_t seen = 0;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_ANALOG;
            slotIdxOut = i;
            return true;
        }
        ++seen;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_DIGITAL_INPUT;
            slotIdxOut = slotIdx;
            return true;
        }
        ++seen;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_DIGITAL_OUTPUT;
            slotIdxOut = slotIdx;
            return true;
        }
        ++seen;
    }
    return false;
}

bool IOModule::buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!ep || !out || len == 0) return false;
    if ((ep->capabilities() & IO_CAP_READ) == 0) return false;

    IOEndpointValue v{};
    bool ok = ep->read(v);
    if (!ok) v.valid = false;

    const char* id = ep->id();
    const char* label = endpointLabel(id);
    int wrote = snprintf(out, len, "{\"id\":\"%s\",\"name\":\"%s\",\"available\":%s,\"value\":",
                         (id && id[0] != '\0') ? id : "",
                         (label && label[0] != '\0') ? label : ((id && id[0] != '\0') ? id : ""),
                         v.valid ? "true" : "false");
    if (wrote < 0 || (size_t)wrote >= len) return false;
    size_t used = (size_t)wrote;

    if (!v.valid) {
        wrote = snprintf(out + used, len - used, "null");
    } else if (v.valueType == IO_EP_VALUE_BOOL) {
        wrote = snprintf(out + used, len - used, "%s", v.v.b ? "true" : "false");
    } else if (v.valueType == IO_EP_VALUE_FLOAT) {
        wrote = snprintf(out + used, len - used, "%.3f", (double)v.v.f);
    } else if (v.valueType == IO_EP_VALUE_INT32) {
        wrote = snprintf(out + used, len - used, "%ld", (long)v.v.i);
    } else {
        wrote = snprintf(out + used, len - used, "null");
    }
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
    used += (size_t)wrote;

    wrote = snprintf(out + used, len - used, ",\"ts\":%lu}", (unsigned long)millis());
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;

    // Ensure one initial publish even if endpoint timestamp has not been set yet.
    maxTsOut = (v.timestampMs == 0U) ? 1U : v.timestampMs;
    return true;
}

const char* IOModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return nullptr;

    static char suffix[24];
    if (routeType == ROUTE_ANALOG) {
        snprintf(suffix, sizeof(suffix), "rt/io/input/a%u", (unsigned)slotIdx);
    } else {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (routeType == ROUTE_DIGITAL_INPUT) {
            snprintf(suffix, sizeof(suffix), "rt/io/input/i%u", (unsigned)s.logicalIdx);
        } else {
            snprintf(suffix, sizeof(suffix), "rt/io/output/d%u", (unsigned)s.logicalIdx);
        }
    }
    return suffix;
}

RuntimeRouteClass IOModule::runtimeSnapshotClass(uint8_t idx) const
{
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) {
        return RuntimeRouteClass::NumericThrottled;
    }
    (void)slotIdx;
    return (routeType == ROUTE_DIGITAL_OUTPUT)
        ? RuntimeRouteClass::ActuatorImmediate
        : RuntimeRouteClass::NumericThrottled;
}

bool IOModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    if (key < DATAKEY_IO_BASE || key >= (DataKey)(DATAKEY_IO_BASE + IO_MAX_ENDPOINTS)) return false;

    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return false;

    IOEndpoint* ep = nullptr;
    if (routeType == ROUTE_ANALOG) {
        ep = static_cast<IOEndpoint*>(analogSlots_[slotIdx].endpoint);
    } else if (routeType == ROUTE_DIGITAL_INPUT || routeType == ROUTE_DIGITAL_OUTPUT) {
        ep = digitalSlots_[slotIdx].endpoint;
    } else {
        return false;
    }
    if (!ep || !ep->id()) return false;

    uint8_t endpointIdx = 0;
    if (!endpointIndexFromId_(ep->id(), endpointIdx)) return false;
    return key == (DataKey)(DATAKEY_IO_BASE + endpointIdx);
}

bool IOModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return false;

    IOEndpoint* ep = nullptr;
    if (routeType == ROUTE_ANALOG) {
        ep = static_cast<IOEndpoint*>(analogSlots_[slotIdx].endpoint);
    } else {
        ep = digitalSlots_[slotIdx].endpoint;
    }
    return buildEndpointSnapshot_(ep, out, len, maxTsOut);
}

bool IOModule::buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;

    size_t used = 0;
    int wrote = snprintf(out, len, "{");
    if (wrote < 0 || (size_t)wrote >= len) return false;
    used += (size_t)wrote;

    bool first = true;
    uint32_t maxTs = 0;
    for (uint8_t i = 0; i < registry_.count(); ++i) {
        IOEndpoint* ep = registry_.at(i);
        if (!ep) continue;
        if ((ep->capabilities() & IO_CAP_READ) == 0) continue;

        const char* id = ep->id();
        if (!id || id[0] == '\0') continue;
        if (inputGroup && !isInputEndpointIdLocal(id)) continue;
        if (!inputGroup && !isOutputEndpointIdLocal(id)) continue;

        IOEndpointValue v{};
        bool ok = ep->read(v);
        if (!ok) v.valid = false;

        const char* label = endpointLabel(id);
        wrote = snprintf(out + used, len - used, "%s\"%s\":{\"name\":\"%s\",\"available\":%s,\"value\":",
                         first ? "" : ",",
                         id,
                         (label && label[0] != '\0') ? label : id,
                         v.valid ? "true" : "false");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;
        first = false;

        if (!v.valid) {
            wrote = snprintf(out + used, len - used, "null");
        } else if (v.valueType == IO_EP_VALUE_BOOL) {
            wrote = snprintf(out + used, len - used, "%s", v.v.b ? "true" : "false");
        } else if (v.valueType == IO_EP_VALUE_FLOAT) {
            wrote = snprintf(out + used, len - used, "%.3f", (double)v.v.f);
        } else if (v.valueType == IO_EP_VALUE_INT32) {
            wrote = snprintf(out + used, len - used, "%ld", (long)v.v.i);
        } else {
            wrote = snprintf(out + used, len - used, "null");
        }
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        wrote = snprintf(out + used, len - used, "}");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        if (v.timestampMs > maxTs) maxTs = v.timestampMs;
    }

    wrote = snprintf(out + used, len - used, ",\"ts\":%lu}", (unsigned long)millis());
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;

    maxTsOut = maxTs;
    return true;
}

bool IOModule::tickFastAds_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->adsInternalProvider_.tick(nowMs);
    self->adsExternalProvider_.tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_ADS_INTERNAL_SINGLE || src == IO_SRC_ADS_EXTERNAL_DIFF) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickSlowDs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->dsWaterProvider_.tick(nowMs);
    self->dsAirProvider_.tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_DS18_WATER || src == IO_SRC_DS18_AIR) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickDigitalInputs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (!self->digitalSlots_[i].used) continue;
        if (self->digitalSlots_[i].kind != DIGITAL_SLOT_INPUT) continue;
        (void)self->processDigitalInputDefinition_(i, nowMs);
    }
    self->pollPulseOutputs_(nowMs);
    return true;
}

const IOAnalogProvider* IOModule::analogProviderForSource_(uint8_t source) const
{
    // Kernel-side routing stays on compact source ids; provider binding happens once in runtime setup.
    if (source == IO_SRC_ADS_INTERNAL_SINGLE) return &adsInternalProvider_;
    if (source == IO_SRC_ADS_EXTERNAL_DIFF) return &adsExternalProvider_;
    if (source == IO_SRC_DS18_WATER) return &dsWaterProvider_;
    if (source == IO_SRC_DS18_AIR) return &dsAirProvider_;
    return nullptr;
}

bool IOModule::processAnalogDefinition_(uint8_t idx, uint32_t nowMs)
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    AnalogSlot& slot = analogSlots_[idx];
    if (!slot.used || !slot.endpoint) return false;

    const IOAnalogProvider* provider = analogProviderForSource_(slot.source);
    if (!provider || !provider->isBound()) return false;

    IOAnalogSample sample{};
    if (!provider->readSample(slot.channel, sample)) {
        const bool isDsSource =
            (slot.source == IO_SRC_DS18_WATER) || (slot.source == IO_SRC_DS18_AIR);
        if (isDsSource) {
            // Surface DS18 disconnect/failure once on transition to invalid; keep timestamp stable while invalid.
            if (slot.lastRoundedValid) {
                slot.endpoint->update(slot.lastRounded, false, nowMs);
                slot.lastRoundedValid = false;
            }
        }
        return false;
    }
    float raw = sample.value;
    int16_t rawBinary = sample.raw;
    uint32_t sampleSeq = sample.seq;
    bool hasSampleSeq = sample.hasSeq;

    // ADS values are processed only when a fresh sample arrives for that channel/pair.
    if (hasSampleSeq) {
        if (slot.lastAdsSampleSeqValid && sampleSeq == slot.lastAdsSampleSeq) return false;
        slot.lastAdsSampleSeq = sampleSeq;
        slot.lastAdsSampleSeqValid = true;
    }

    float filtered = slot.median.update(raw);
    float calibrated = (slot.def.c0 * filtered) + slot.def.c1;
    float rounded = ioRoundToPrecision(calibrated, slot.def.precision);

    // Trace pH/ORP/PSI calculation chain with configurable periodic ticker.
    bool isAdsSource = (slot.source == IO_SRC_ADS_INTERNAL_SINGLE) ||
                       (slot.source == IO_SRC_ADS_EXTERNAL_DIFF);
    if (cfgData_.traceEnabled && isAdsSource && idx < 3) {
        uint32_t periodMs =
            (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : Limits::IoTracePeriodMs;
        uint32_t& lastMs = analogCalcLogLastMs_[idx];
        if (lastMs == 0U || (uint32_t)(nowMs - lastMs) >= periodMs) {
            const char* sensor = (idx == 0) ? "ORP" : ((idx == 1) ? "pH" : "PSI");
            const char sourceMark = (slot.source == IO_SRC_ADS_INTERNAL_SINGLE) ? 'I' : 'E';
            LOGI("Calc %c %-3s raw_bin=%7d raw_V=%10.6f median_V=%10.6f coeff=%9.3f rounded=%9.3f",
                 sourceMark,
                 sensor,
                 (int)rawBinary,
                 (double)raw,
                 (double)filtered,
                 (double)calibrated,
                 (double)rounded);
            lastMs = nowMs;
        }
    }

    slot.endpoint->update(rounded, true, nowMs);

    if (!slot.lastRoundedValid || rounded != slot.lastRounded) {
        slot.lastRounded = rounded;
        slot.lastRoundedValid = true;
        markIoCycleChanged_(slot.ioId);
        if (slot.def.onValueChanged) {
            slot.def.onValueChanged(slot.def.onValueCtx, rounded);
        }
    }

    return true;
}

bool IOModule::processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs)
{
    if (slotIdx >= MAX_DIGITAL_SLOTS) return false;
    DigitalSlot& slot = digitalSlots_[slotIdx];
    if (!slot.used || slot.kind != DIGITAL_SLOT_INPUT || !slot.endpoint) return false;
    if (slot.endpoint->type() != IO_EP_DIGITAL_SENSOR) return false;

    DigitalSensorEndpoint* inputEp = static_cast<DigitalSensorEndpoint*>(slot.endpoint);

    if (slot.inDef.mode == IO_DIGITAL_INPUT_COUNTER) {
        if (!slot.provider.isBound()) return false;
        IDigitalCounterDriver* counterDriver = static_cast<IDigitalCounterDriver*>(slot.provider.ctx);
        if (!counterDriver) return false;

        int32_t count = 0;
        if (!counterDriver->readCount(count)) {
            if (slot.lastValid) {
                inputEp->updateCount(slot.lastCount, false, nowMs);
                slot.lastValid = false;
            }
            return false;
        }

        const int32_t totalCount = slot.counterPersistedTotal + count;
        (void)persistCounterTotalIfNeeded_(slot, totalCount);

        const bool changed = (!slot.lastValid) || (slot.lastCount != totalCount);
        if (changed) {
            inputEp->updateCount(totalCount, true, nowMs);
            slot.lastCount = totalCount;
            slot.lastValid = true;
            if (dataStore_) {
                uint8_t rtIdx = 0;
                if (endpointIndexFromId_(slot.endpointId, rtIdx)) {
                    (void)setIoEndpointInt(*dataStore_, rtIdx, totalCount, nowMs);
                }
            }
            markIoCycleChanged_(slot.ioId);
            if (slot.inDef.onCounterChanged) {
                slot.inDef.onCounterChanged(slot.inDef.onCounterCtx, totalCount);
            }
        }
        return true;
    }

    if (!slot.provider.isBound()) return false;

    bool on = false;
    if (!slot.provider.read(on)) {
        // Transition to invalid only once; avoid timestamp churn while input remains unreadable.
        if (slot.lastValid) {
            inputEp->update(false, false, nowMs);
            slot.lastValid = false;
        }
        return false;
    }

    const bool changed = (!slot.lastValid) || (slot.lastValue != on);
    if (changed) {
        inputEp->update(on, true, nowMs);
        slot.lastValue = on;
        slot.lastValid = true;
        if (dataStore_) {
            uint8_t rtIdx = 0;
            if (endpointIndexFromId_(slot.endpointId, rtIdx)) {
                (void)setIoEndpointBool(*dataStore_, rtIdx, on, nowMs);
            }
        }
        markIoCycleChanged_(slot.ioId);
        if (slot.inDef.onValueChanged) {
            slot.inDef.onValueChanged(slot.inDef.onValueCtx, on);
        }
    }

    return true;
}

int32_t IOModule::sanitizeAnalogPrecision_(int32_t precision) const
{
    if (precision < 0) return 0;
    if (precision > 6) return 6;
    return precision;
}

void IOModule::forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs)
{
    if (analogIdx >= MAX_ANALOG_ENDPOINTS) return;
    AnalogSlot& slot = analogSlots_[analogIdx];
    if (!slot.used || !slot.endpoint) return;

    IOEndpointValue v{};
    if (!slot.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return;

    float republished = ioRoundToPrecision(v.v.f, slot.def.precision);
    slot.endpoint->update(republished, true, nowMs);
    if (dataStore_) {
        (void)setIoEndpointFloat(*dataStore_, analogIdx, republished, nowMs);
    }
}

void IOModule::refreshAnalogConfigState_()
{
    if (!analogPrecisionLastInit_) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            int32_t p = sanitizeAnalogPrecision_(analogCfg_[i].precision);
            analogPrecisionLast_[i] = p;
        }
        analogPrecisionLastInit_ = true;
        return;
    }

    bool changed = false;
    uint16_t changedMask = 0;
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        int32_t p = sanitizeAnalogPrecision_(analogCfg_[i].precision);
        if (analogPrecisionLast_[i] == p) continue;
        analogPrecisionLast_[i] = p;
        if (runtimeReady_ && i < MAX_ANALOG_ENDPOINTS && analogSlots_[i].used) {
            analogSlots_[i].def.precision = p;
        }
        changedMask |= (uint16_t)(1u << i);
        changed = true;
    }

    if (changed) {
        LOGI("Input precision changed -> publish runtime snapshot");
        const uint32_t nowMs = millis();
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            if ((changedMask & (uint16_t)(1u << i)) == 0) continue;
            forceAnalogSnapshotPublish_(i, nowMs);
        }
        analogConfigDirtyMask_ |= changedMask;
    }
}

uint8_t IOModule::ioCount_() const
{
    uint8_t count = 0;
    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) ++count;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) ++count;
    }
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (analogSlots_[i].used) ++count;
    }
    return count;
}

IoStatus IOModule::ioIdAt_(uint8_t index, IoId* outId) const
{
    if (!outId) return IO_ERR_INVALID_ARG;
    uint8_t seen = 0;

    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) continue;
        if (seen == index) {
            *outId = digitalSlots_[slotIdx].ioId;
            return IO_OK;
        }
        ++seen;
    }

    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) continue;
        if (seen == index) {
            *outId = digitalSlots_[slotIdx].ioId;
            return IO_OK;
        }
        ++seen;
    }

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        if (seen == index) {
            *outId = analogSlots_[i].ioId;
            return IO_OK;
        }
        ++seen;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioMeta_(IoId id, IoEndpointMeta* outMeta) const
{
    if (!outMeta) return IO_ERR_INVALID_ARG;
    *outMeta = IoEndpointMeta{};
    outMeta->id = id;

    uint8_t slotIdx = 0xFF;
    if (findDigitalSlotByIoId_(id, slotIdx)) {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used) return IO_ERR_UNKNOWN_ID;

        outMeta->kind = (s.kind == DIGITAL_SLOT_OUTPUT) ? IO_KIND_DIGITAL_OUT : IO_KIND_DIGITAL_IN;
        outMeta->valueType = (s.kind == DIGITAL_SLOT_OUTPUT)
            ? IO_VAL_BOOL
            : ((s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_VAL_INT32 : IO_VAL_BOOL);
        outMeta->backend = s.backend;
        outMeta->channel = s.channel;
        outMeta->capabilities = (s.kind == DIGITAL_SLOT_OUTPUT) ? (IO_CAP_R | IO_CAP_W) : IO_CAP_R;

        const char* name = nullptr;
        if (s.kind == DIGITAL_SLOT_OUTPUT && s.logicalIdx < DIGITAL_CFG_SLOTS) {
            name = digitalCfg_[s.logicalIdx].name;
        } else if (s.kind == DIGITAL_SLOT_INPUT) {
            if (s.logicalIdx < MAX_DIGITAL_INPUTS && digitalInCfg_[s.logicalIdx].name[0] != '\0') {
                name = digitalInCfg_[s.logicalIdx].name;
            } else {
                name = s.inDef.id;
            }
        }
        if (!name || name[0] == '\0') name = s.endpointId;
        if (!name) name = "";
        strncpy(outMeta->name, name, sizeof(outMeta->name) - 1);
        outMeta->name[sizeof(outMeta->name) - 1] = '\0';
        return IO_OK;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        const AnalogSlot& s = analogSlots_[analogIdx];
        if (!s.used) return IO_ERR_UNKNOWN_ID;

        outMeta->kind = IO_KIND_ANALOG_IN;
        outMeta->valueType = IO_VAL_FLOAT;
        outMeta->capabilities = IO_CAP_R;
        outMeta->channel = s.channel;
        outMeta->backend = s.backend;
        outMeta->precision = s.def.precision;
        outMeta->minValid = 0.0f;
        outMeta->maxValid = 0.0f;

        const char* name = (analogIdx < ANALOG_CFG_SLOTS) ? analogCfg_[analogIdx].name : nullptr;
        if (!name || name[0] == '\0') name = s.def.id;
        if (!name) name = "";
        strncpy(outMeta->name, name, sizeof(outMeta->name) - 1);
        outMeta->name[sizeof(outMeta->name) - 1] = '\0';
        return IO_OK;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioReadValue_(IoId id, IoValue* outValue) const
{
    if (!outValue) return IO_ERR_INVALID_ARG;
    *outValue = IoValue{};

    uint8_t slotIdx = 0xFF;
    if (findDigitalSlotByIoId_(id, slotIdx)) {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

        IOEndpointValue v{};
        if (!s.endpoint->read(v) || !v.valid) return IO_ERR_NOT_READY;

        outValue->valid = 1U;
        outValue->tsMs = v.timestampMs;
        outValue->cycleSeq = lastCycle_.seq;
        if (v.valueType == IO_EP_VALUE_BOOL) {
            outValue->type = IO_VAL_BOOL;
            outValue->v.b = v.v.b ? 1U : 0U;
            return IO_OK;
        }
        if (v.valueType == IO_EP_VALUE_INT32) {
            outValue->type = IO_VAL_INT32;
            outValue->v.i32 = v.v.i;
            return IO_OK;
        }
        return IO_ERR_TYPE_MISMATCH;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        const AnalogSlot& s = analogSlots_[analogIdx];
        if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

        IOEndpointValue v{};
        if (!s.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return IO_ERR_NOT_READY;

        outValue->valid = 1U;
        outValue->type = IO_VAL_FLOAT;
        outValue->tsMs = v.timestampMs;
        outValue->cycleSeq = lastCycle_.seq;
        outValue->v.f = v.v.f;
        return IO_OK;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioReadDigital_(IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq) const
{
    if (!outOn) return IO_ERR_INVALID_ARG;

    uint8_t slotIdx = 0xFF;
    if (!findDigitalSlotByIoId_(id, slotIdx)) return IO_ERR_UNKNOWN_ID;
    const DigitalSlot& s = digitalSlots_[slotIdx];
    if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue v{};
    if (!s.endpoint->read(v) || !v.valid) return IO_ERR_NOT_READY;
    if (v.valueType != IO_EP_VALUE_BOOL) return IO_ERR_TYPE_MISMATCH;

    *outOn = v.v.b ? 1U : 0U;
    if (outTsMs) *outTsMs = v.timestampMs;
    if (outSeq) *outSeq = lastCycle_.seq;
    return IO_OK;
}

IoStatus IOModule::ioWriteDigital_(IoId id, uint8_t on, uint32_t tsMs)
{
    uint8_t slotIdx = 0xFF;
    if (!findDigitalSlotByIoId_(id, slotIdx)) return IO_ERR_UNKNOWN_ID;
    DigitalSlot& s = digitalSlots_[slotIdx];
    if (!s.used) return IO_ERR_UNKNOWN_ID;
    if (s.kind != DIGITAL_SLOT_OUTPUT) return IO_ERR_READ_ONLY;
    if (!s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue in{};
    in.timestampMs = (tsMs == 0) ? millis() : tsMs;
    in.valueType = IO_EP_VALUE_BOOL;
    in.v.b = (on != 0U);
    in.valid = true;
    if (!s.endpoint->write(in)) return IO_ERR_HW;

    if (dataStore_) {
        uint8_t rtIdx = 0;
        if (endpointIndexFromId_(s.endpointId, rtIdx)) {
            (void)setIoEndpointBool(*dataStore_, rtIdx, in.v.b, in.timestampMs);
        }
    }

    markIoCycleChanged_(s.ioId);
    return IO_OK;
}

IoStatus IOModule::ioReadAnalog_(IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq) const
{
    if (!outValue) return IO_ERR_INVALID_ARG;
    if (id < IO_ID_AI_BASE || id >= IO_ID_AI_MAX) return IO_ERR_UNKNOWN_ID;

    const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
    const AnalogSlot& s = analogSlots_[analogIdx];
    if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue v{};
    if (!s.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return IO_ERR_NOT_READY;

    *outValue = v.v.f;
    if (outTsMs) *outTsMs = v.timestampMs;
    if (outSeq) *outSeq = lastCycle_.seq;
    return IO_OK;
}

IoStatus IOModule::ioTick_(uint32_t nowMs)
{
    refreshAnalogConfigState_();

    if (!cfgData_.enabled) return IO_ERR_NOT_READY;
    if (!runtimeReady_) return IO_ERR_NOT_READY;

    if (pcfLastEnabled_ != cfgData_.pcfEnabled) {
        if (!cfgData_.pcfEnabled && ledMaskEp_) {
            uint8_t offLogical = 0;
            uint8_t offPhysical = pcfPhysicalFromLogical_(offLogical);
            ledMaskEp_->setMask(offPhysical, nowMs);
            pcfLogicalMask_ = offLogical;
            pcfLogicalValid_ = true;
            pcfEnableNeedsReinitWarned_ = false;
        } else if (cfgData_.pcfEnabled) {
            if (ledMaskEp_) {
                setLedMask_(cfgData_.pcfMaskDefault, nowMs);
                pcfEnableNeedsReinitWarned_ = false;
            } else if (!pcfEnableNeedsReinitWarned_) {
                LOGW("pcf_enabled changed at runtime but PCF endpoint was not provisioned at init; reboot required");
                pcfEnableNeedsReinitWarned_ = true;
            }
        }
        pcfLastEnabled_ = cfgData_.pcfEnabled;
    }

    beginIoCycle_(nowMs);
    scheduler_.tick(nowMs);
    return IO_OK;
}

IoStatus IOModule::ioLastCycle_(IoCycleInfo* outCycle) const
{
    if (!outCycle) return IO_ERR_INVALID_ARG;
    *outCycle = lastCycle_;
    return IO_OK;
}

bool IOModule::getLedMaskSvc_(uint8_t* mask) const
{
    if (!mask) return false;
    return getLedMask_(*mask);
}

bool IOModule::setLedMask_(uint8_t mask, uint32_t tsMs)
{
    if (!cfgData_.pcfEnabled) return false;
    if (!ledMaskEp_) return false;
    uint8_t physical = pcfPhysicalFromLogical_(mask);
    bool ok = ledMaskEp_->setMask(physical, tsMs);
    if (ok) {
        pcfLogicalMask_ = mask;
        pcfLogicalValid_ = true;
    }
    return ok;
}

bool IOModule::turnLedOn_(uint8_t bit, uint32_t tsMs)
{
    if (!cfgData_.pcfEnabled) return false;
    if (bit > 7) return false;
    uint8_t mask = 0;
    if (!getLedMask_(mask)) mask = 0;
    mask = (uint8_t)(mask | (uint8_t)(1u << bit));
    return setLedMask_(mask, tsMs);
}

bool IOModule::turnLedOff_(uint8_t bit, uint32_t tsMs)
{
    if (!cfgData_.pcfEnabled) return false;
    if (bit > 7) return false;
    uint8_t mask = 0;
    if (!getLedMask_(mask)) mask = 0;
    mask = (uint8_t)(mask & (uint8_t)~(1u << bit));
    return setLedMask_(mask, tsMs);
}

bool IOModule::getLedMask_(uint8_t& mask) const
{
    if (!cfgData_.pcfEnabled) return false;
    if (pcfLogicalValid_) {
        mask = pcfLogicalMask_;
        return true;
    }
    if (!ledMaskEp_) return false;
    uint8_t physical = 0;
    if (!ledMaskEp_->getMask(physical)) return false;
    mask = pcfLogicalFromPhysical_(physical);
    return true;
}

uint8_t IOModule::pcfPhysicalFromLogical_(uint8_t logicalMask) const
{
    return cfgData_.pcfActiveLow ? (uint8_t)~logicalMask : logicalMask;
}

uint8_t IOModule::pcfLogicalFromPhysical_(uint8_t physicalMask) const
{
    return cfgData_.pcfActiveLow ? (uint8_t)~physicalMask : physicalMask;
}

const IOBindingPortSpec* IOModule::bindingPortSpec_(PhysicalPortId portId) const
{
    if (portId == IO_PORT_INVALID || !bindingPorts_ || bindingPortCount_ == 0) return nullptr;
    for (uint8_t i = 0; i < bindingPortCount_; ++i) {
        if (bindingPorts_[i].portId == portId) return &bindingPorts_[i];
    }
    return nullptr;
}

bool IOModule::resolveAnalogBinding_(PhysicalPortId portId, uint8_t& sourceOut, uint8_t& channelOut, uint8_t& backendOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;

    switch (spec->kind) {
        case IO_PORT_KIND_ADS_INTERNAL_SINGLE:
            sourceOut = IO_SRC_ADS_INTERNAL_SINGLE;
            channelOut = spec->param0;
            backendOut = IO_BACKEND_ADS1115_INT;
            return true;
        case IO_PORT_KIND_ADS_EXTERNAL_DIFF:
            sourceOut = IO_SRC_ADS_EXTERNAL_DIFF;
            channelOut = spec->param0;
            backendOut = IO_BACKEND_ADS1115_EXT_DIFF;
            return true;
        case IO_PORT_KIND_DS18_WATER:
            sourceOut = IO_SRC_DS18_WATER;
            channelOut = 0U;
            backendOut = IO_BACKEND_DS18B20;
            return true;
        case IO_PORT_KIND_DS18_AIR:
            sourceOut = IO_SRC_DS18_AIR;
            channelOut = 0U;
            backendOut = IO_BACKEND_DS18B20;
            return true;
        default:
            return false;
    }
}

bool IOModule::resolveDigitalInputBinding_(PhysicalPortId portId, uint8_t& pinOut, uint8_t& backendOut, uint8_t& channelOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;
    if (spec->kind != IO_PORT_KIND_GPIO_INPUT) return false;

    pinOut = spec->param0;
    backendOut = IO_BACKEND_GPIO;
    channelOut = spec->param0;
    return true;
}

bool IOModule::resolveDigitalOutputBinding_(PhysicalPortId portId,
                                            uint8_t& pinOut,
                                            uint8_t& backendOut,
                                            uint8_t& channelOut,
                                            bool& usesPcfOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;

    if (spec->kind == IO_PORT_KIND_GPIO_OUTPUT) {
        pinOut = spec->param0;
        backendOut = IO_BACKEND_GPIO;
        channelOut = spec->param0;
        usesPcfOut = false;
        return true;
    }
    if (spec->kind == IO_PORT_KIND_PCF8574_OUTPUT) {
        pinOut = 0U;
        backendOut = IO_BACKEND_PCF8574;
        channelOut = spec->param0;
        usesPcfOut = true;
        return true;
    }
    return false;
}

bool IOModule::resolveDsBusAddress_(OneWireBus* bus, const char* runtimeKey, uint8_t outAddr[8])
{
    if (!bus || !runtimeKey || !outAddr) return false;

    bus->begin();

    size_t len = 0U;
    if (cfgStore_ && cfgStore_->readRuntimeBlob(runtimeKey, outAddr, 8U, &len) && len == 8U) {
        return true;
    }

    if (bus->deviceCount() != 1U) return false;
    if (!bus->getAddress(0, outAddr)) return false;

    if (cfgStore_) {
        (void)cfgStore_->writeRuntimeBlob(runtimeKey, outAddr, 8U);
    }
    return true;
}

bool IOModule::loadCounterPersistedTotal_(uint8_t logicalIdx, int32_t& totalOut) const
{
    if (!cfgStore_) return false;

    char key[16];
    snprintf(key, sizeof(key), NvsKeys::Io::CounterRuntimeFmt, (unsigned)logicalIdx);
    size_t len = 0U;
    int32_t stored = 0;
    if (!cfgStore_->readRuntimeBlob(key, &stored, sizeof(stored), &len) || len != sizeof(stored)) {
        return false;
    }
    totalOut = stored;
    return true;
}

bool IOModule::persistCounterTotalIfNeeded_(DigitalSlot& slot, int32_t totalCount)
{
    static constexpr int32_t kCounterPersistPulseDelta = 32;

    if (!cfgStore_) return false;
    if (slot.kind != DIGITAL_SLOT_INPUT || slot.inDef.mode != IO_DIGITAL_INPUT_COUNTER) return false;
    if (totalCount < slot.counterLastFlushedTotal) return false;
    if ((totalCount - slot.counterLastFlushedTotal) < kCounterPersistPulseDelta) return false;

    char key[16];
    snprintf(key, sizeof(key), NvsKeys::Io::CounterRuntimeFmt, (unsigned)slot.logicalIdx);
    if (!cfgStore_->writeRuntimeBlob(key, &totalCount, sizeof(totalCount))) {
        return false;
    }
    slot.counterLastFlushedTotal = totalCount;
    return true;
}

bool IOModule::configureRuntime_()
{
    if (runtimeReady_) return true;
    if (!cfgData_.enabled) return false;

    // Concrete bus/driver assembly is centralized here so the rest of the module can stay on kernel types.
    i2cBus_.begin(cfgData_.i2cSda, cfgData_.i2cScl);
    const bool ads48Present = i2cBus_.probe(0x48);
    const bool ads49Present = i2cBus_.probe(0x49);
    LOGI("ADS1115 probe 0x48: %s", ads48Present ? "found" : "not found");
    LOGI("ADS1115 probe 0x49: %s", ads49Present ? "found" : "not found");

    bool needAdsInternal = false;
    bool needAdsExternal = false;
    bool needDsWater = false;
    bool needDsAir = false;

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        analogSlots_[i].ioId = (IoId)(IO_ID_AI_BASE + i);
        analogSlots_[i].source = 0xFFu;
        analogSlots_[i].channel = 0U;
        analogSlots_[i].backend = IO_BACKEND_GPIO;

        if (i < ANALOG_CFG_SLOTS) {
            snprintf(analogSlots_[i].def.id, sizeof(analogSlots_[i].def.id), "a%u", (unsigned)i);
            analogSlots_[i].def.bindingPort = analogCfg_[i].bindingPort;
            analogSlots_[i].def.c0 = analogCfg_[i].c0;
            analogSlots_[i].def.c1 = analogCfg_[i].c1;
            analogSlots_[i].def.precision = analogCfg_[i].precision;

            uint8_t source = 0xFFu;
            uint8_t channel = 0U;
            uint8_t backend = IO_BACKEND_GPIO;
            if (resolveAnalogBinding_(analogSlots_[i].def.bindingPort, source, channel, backend)) {
                analogSlots_[i].source = source;
                analogSlots_[i].channel = channel;
                analogSlots_[i].backend = backend;
            } else {
                LOGW("Analog %s unresolved binding_port=%u",
                     analogSlots_[i].def.id,
                     (unsigned)analogSlots_[i].def.bindingPort);
            }

            if (i < 3 && analogSlots_[i].source != 0xFFu) {
                LOGI("Analog map %s binding_port=%u source=%u channel=%u",
                     analogSlots_[i].def.id,
                     (unsigned)analogSlots_[i].def.bindingPort,
                     (unsigned)analogSlots_[i].source,
                     (unsigned)analogSlots_[i].channel);
            }
        }

        if (analogSlots_[i].source == IO_SRC_ADS_INTERNAL_SINGLE) needAdsInternal = true;
        else if (analogSlots_[i].source == IO_SRC_ADS_EXTERNAL_DIFF) needAdsExternal = true;
        else if (analogSlots_[i].source == IO_SRC_DS18_WATER) needDsWater = true;
        else if (analogSlots_[i].source == IO_SRC_DS18_AIR) needDsAir = true;

        analogSlots_[i].endpoint = allocAnalogEndpoint_(analogSlots_[i].def.id);
        if (!analogSlots_[i].endpoint) continue;
        registry_.add(analogSlots_[i].endpoint);
    }

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (!digitalSlots_[i].used) continue;
        DigitalSlot& s = digitalSlots_[i];
        s.owner = this;
        s.ioId = (s.kind == DIGITAL_SLOT_OUTPUT)
                   ? (IoId)(IO_ID_DO_BASE + s.logicalIdx)
                   : (IoId)(IO_ID_DI_BASE + s.logicalIdx);

        if (s.kind == DIGITAL_SLOT_INPUT) {
            const uint8_t cfgIdx = s.logicalIdx;
            if (cfgIdx < MAX_DIGITAL_INPUTS) {
                if (digitalInCfg_[cfgIdx].name[0] != '\0') {
                    strncpy(s.inDef.id, digitalInCfg_[cfgIdx].name, sizeof(s.inDef.id) - 1);
                    s.inDef.id[sizeof(s.inDef.id) - 1] = '\0';
                }
                s.inDef.bindingPort = digitalInCfg_[cfgIdx].bindingPort;
                s.inDef.activeHigh = digitalInCfg_[cfgIdx].activeHigh;
                uint8_t pull = digitalInCfg_[cfgIdx].pullMode;
                if (pull > IO_PULL_DOWN) pull = IO_PULL_NONE;
                s.inDef.pullMode = pull;
                s.inDef.mode = digitalInCfg_[cfgIdx].mode;
                s.inDef.edgeMode = digitalInCfg_[cfgIdx].edgeMode;
                s.inDef.counterDebounceUs = digitalInCfg_[cfgIdx].counterDebounceUs;
            }

            snprintf(s.endpointId, sizeof(s.endpointId), "i%u", (unsigned)s.logicalIdx);
            uint8_t pin = 0U;
            uint8_t backend = IO_BACKEND_GPIO;
            uint8_t channel = 0U;
            if (!resolveDigitalInputBinding_(s.inDef.bindingPort, pin, backend, channel)) {
                LOGW("Digital input %s unresolved binding_port=%u",
                     s.endpointId,
                     (unsigned)s.inDef.bindingPort);
                continue;
            }
            s.backend = backend;
            s.channel = channel;
            IDigitalCounterDriver* driver = allocGpioDriver_(
                s.endpointId,
                pin,
                false,
                s.inDef.activeHigh,
                s.inDef.pullMode,
                s.inDef.mode == IO_DIGITAL_INPUT_COUNTER,
                s.inDef.edgeMode,
                s.inDef.counterDebounceUs
            );
            if (!driver) continue;

            s.provider = makeDigitalProvider(driver);
            if (!s.provider.begin()) continue;

            const uint8_t valueType = (s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_EP_VALUE_INT32 : IO_EP_VALUE_BOOL;
            s.endpoint = allocDigitalSensorEndpoint_(s.endpointId, valueType);
            if (!s.endpoint) continue;
            if (s.inDef.mode == IO_DIGITAL_INPUT_COUNTER) {
                int32_t initialTotal = 0;
                int32_t persistedTotal = 0;
                if (loadCounterPersistedTotal_(s.logicalIdx, persistedTotal)) {
                    s.counterPersistedTotal = persistedTotal;
                    s.counterLastFlushedTotal = persistedTotal;
                    initialTotal = persistedTotal;
                }
                s.lastCount = initialTotal;
                s.lastValid = false;
                static_cast<DigitalSensorEndpoint*>(s.endpoint)->updateCount(initialTotal, true, millis());
            }
            registry_.add(s.endpoint);
            (void)processDigitalInputDefinition_(i, millis());
            continue;
        }

        const uint8_t cfgIdx = s.logicalIdx;
        if (cfgIdx < DIGITAL_CFG_SLOTS) {
            snprintf(s.outDef.id, sizeof(s.outDef.id), "d%u", (unsigned)cfgIdx);
            s.outDef.bindingPort = digitalCfg_[cfgIdx].bindingPort;
            s.outDef.activeHigh = digitalCfg_[cfgIdx].activeHigh;
            s.outDef.initialOn = digitalCfg_[cfgIdx].initialOn;
            s.outDef.momentary = digitalCfg_[cfgIdx].momentary;
            int32_t p = digitalCfg_[cfgIdx].pulseMs;
            if (p <= 0) p = 500;
            if (p > 60000) p = 60000;
            s.outDef.pulseMs = (uint16_t)p;
        } else {
            snprintf(s.outDef.id, sizeof(s.outDef.id), "d%u", (unsigned)s.logicalIdx);
        }

        strncpy(s.endpointId, s.outDef.id, sizeof(s.endpointId) - 1);
        s.endpointId[sizeof(s.endpointId) - 1] = '\0';

        uint8_t pin = 0U;
        uint8_t backend = IO_BACKEND_GPIO;
        uint8_t channel = 0U;
        bool usesPcfOut = false;
        if (!resolveDigitalOutputBinding_(s.outDef.bindingPort, pin, backend, channel, usesPcfOut)) {
            LOGW("Digital output %s unresolved binding_port=%u",
                 s.endpointId,
                 (unsigned)s.outDef.bindingPort);
            continue;
        }
        s.backend = backend;
        s.channel = channel;

        IDigitalPinDriver* driver = nullptr;
        if (usesPcfOut) {
            if (!cfgData_.pcfEnabled) {
                LOGW("Digital output %s requires PCF8574 but module is disabled", s.endpointId);
                continue;
            }
            if (!pcfDriver_) {
                IMaskOutputDriver* pcfMaskDriver = allocPcfDriver_("pcf8574", &i2cBus_, cfgData_.pcfAddress);
                if (!pcfMaskDriver) {
                    LOGW("PCF8574 pool exhausted");
                    continue;
                }
                pcfDriver_ = static_cast<Pcf8574Driver*>(pcfMaskDriver);
                if (!makeMaskProvider(pcfDriver_).begin()) {
                    LOGW("PCF8574 not detected at 0x%02X", cfgData_.pcfAddress);
                    pcfDriver_ = nullptr;
                    continue;
                }
            }
            driver = allocPcfBitDriver_(s.outDef.id, pcfDriver_, channel, s.outDef.activeHigh);
        } else {
            driver = allocGpioDriver_(s.outDef.id, pin, true, s.outDef.activeHigh);
        }
        if (!driver) continue;

        s.provider = makeDigitalProvider(driver);
        if (!s.provider.begin()) continue;
        s.provider.write(s.outDef.initialOn);
        s.pulseArmed = false;
        s.pulseDeadlineMs = 0;

        s.endpoint = static_cast<IOEndpoint*>(allocDigitalActuatorEndpoint_(
            s.outDef.id,
            &IOModule::writeDigitalOut_,
            &s
        ));
        if (!s.endpoint) continue;
        registry_.add(s.endpoint);
    }

    Ads1115DriverConfig adsInternalCfg{};
    adsInternalCfg.address = cfgData_.adsInternalAddr;
    adsInternalCfg.gain = (uint8_t)cfgData_.adsGain;
    adsInternalCfg.dataRate = (uint8_t)cfgData_.adsRate;
    adsInternalCfg.pollMs = (cfgData_.adsPollMs < 20) ? 20 : (uint32_t)cfgData_.adsPollMs;
    adsInternalCfg.differentialPairs = false;

    Ads1115DriverConfig adsExternalCfg = adsInternalCfg;
    adsExternalCfg.address = cfgData_.adsExternalAddr;
    adsExternalCfg.differentialPairs = true;

    if (needAdsInternal) {
        IAnalogSourceDriver* driver = allocAdsDriver_("ads_internal", &i2cBus_, adsInternalCfg);
        if (!driver) {
            LOGW("ADS internal pool exhausted");
        } else
        if (!makeAnalogProvider(driver).begin()) {
            LOGW("ADS internal not detected at 0x%02X", cfgData_.adsInternalAddr);
        } else {
            adsInternalProvider_ = makeAnalogProvider(driver);
            if (cfgData_.adsInternalAddr == 0x49) {
                LOGI("ADS1115 found at 0x49 (internal)");
            }
        }
    }

    if (needAdsExternal) {
        IAnalogSourceDriver* driver = allocAdsDriver_("ads_external", &i2cBus_, adsExternalCfg);
        if (!driver) {
            LOGW("ADS external pool exhausted");
        } else
        if (!makeAnalogProvider(driver).begin()) {
            LOGW("ADS external not detected at 0x%02X", cfgData_.adsExternalAddr);
        } else {
            adsExternalProvider_ = makeAnalogProvider(driver);
            if (cfgData_.adsExternalAddr == 0x49) {
                LOGI("ADS1115 found at 0x49 (external)");
            }
        }
    }

    Ds18b20DriverConfig dsCfg{};
    dsCfg.pollMs = (cfgData_.dsPollMs < 750) ? 750 : (uint32_t)cfgData_.dsPollMs;
    dsCfg.conversionWaitMs = 750;

    if (needDsWater && oneWireWater_) {
        oneWireWaterAddrValid_ = resolveDsBusAddress_(oneWireWater_, NvsKeys::Io::DsRomWater, oneWireWaterAddr_);
        if (oneWireWaterAddrValid_) {
            IAnalogSourceDriver* driver = allocDsDriver_("ds18_water", oneWireWater_, oneWireWaterAddr_, dsCfg);
            if (driver) {
                dsWaterProvider_ = makeAnalogProvider(driver);
                (void)dsWaterProvider_.begin();
            } else {
                LOGW("DS18 water pool exhausted");
            }
        } else {
            LOGW("No resolvable DS18B20 found on water OneWire bus");
        }
    }

    if (needDsAir && oneWireAir_) {
        oneWireAirAddrValid_ = resolveDsBusAddress_(oneWireAir_, NvsKeys::Io::DsRomAir, oneWireAirAddr_);
        if (oneWireAirAddrValid_) {
            IAnalogSourceDriver* driver = allocDsDriver_("ds18_air", oneWireAir_, oneWireAirAddr_, dsCfg);
            if (driver) {
                dsAirProvider_ = makeAnalogProvider(driver);
                (void)dsAirProvider_.begin();
            } else {
                LOGW("DS18 air pool exhausted");
            }
        } else {
            LOGW("No resolvable DS18B20 found on air OneWire bus");
        }
    }

    if (cfgData_.pcfEnabled) {
        IMaskOutputDriver* driver = pcfDriver_ ? static_cast<IMaskOutputDriver*>(pcfDriver_)
                                               : allocPcfDriver_("pcf8574_led", &i2cBus_, cfgData_.pcfAddress);
        if (!driver) {
            LOGW("PCF8574 pool exhausted");
        } else {
            if (!pcfDriver_) {
                pcfDriver_ = static_cast<Pcf8574Driver*>(driver);
                if (!makeMaskProvider(driver).begin()) {
                    LOGW("PCF8574 not detected at 0x%02X", cfgData_.pcfAddress);
                    pcfDriver_ = nullptr;
                    driver = nullptr;
                }
            }
        }
        if (driver) {
            ledMaskProvider_ = makeMaskProvider(driver);
            ledMaskEp_ = allocMaskEndpoint_(
                "status_leds_mask",
                [](void* ctx, uint8_t mask) -> bool {
                    return static_cast<IMaskOutputDriver*>(ctx)->writeMask(mask);
                },
                [](void* ctx, uint8_t* mask) -> bool {
                    if (!mask) return false;
                    return static_cast<IMaskOutputDriver*>(ctx)->readMask(*mask);
                },
                driver
            );
            if (ledMaskEp_) {
                registry_.add(ledMaskEp_);
                setLedMask_(cfgData_.pcfMaskDefault, millis());
            }
        }
    }

    IOScheduledJob adsJob{};
    adsJob.id = "ads_fast";
    adsJob.periodMs = (cfgData_.adsPollMs < 20) ? 20 : (uint32_t)cfgData_.adsPollMs;
    adsJob.fn = &IOModule::tickFastAds_;
    adsJob.ctx = this;
    scheduler_.add(adsJob);

    IOScheduledJob dsJob{};
    dsJob.id = "ds_slow";
    dsJob.periodMs = (cfgData_.dsPollMs < 250) ? 250 : (uint32_t)cfgData_.dsPollMs;
    dsJob.fn = &IOModule::tickSlowDs_;
    dsJob.ctx = this;
    scheduler_.add(dsJob);

    IOScheduledJob dinJob{};
    dinJob.id = "din_poll";
    dinJob.periodMs = (cfgData_.digitalPollMs < 20) ? 20 : (uint32_t)cfgData_.digitalPollMs;
    dinJob.fn = &IOModule::tickDigitalInputs_;
    dinJob.ctx = this;
    scheduler_.add(dinJob);

    runtimeReady_ = true;
    pcfLastEnabled_ = cfgData_.pcfEnabled;

    LOGI("I/O ready (ads=%ldms ds=%ldms din=%ldms endpoints=%u pcf=%s)",
         (long)adsJob.periodMs,
         (long)dsJob.periodMs,
         (long)dinJob.periodMs,
         (unsigned)registry_.count(),
         cfgData_.pcfEnabled ? "on" : "off");

    return true;
}

void IOModule::pollPulseOutputs_(uint32_t nowMs)
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (!s.used || s.kind != DIGITAL_SLOT_OUTPUT) continue;
        if (!s.outDef.momentary || !s.pulseArmed || !s.provider.isBound()) continue;
        if ((int32_t)(nowMs - s.pulseDeadlineMs) < 0) continue;
        (void)s.provider.write(false);
        s.pulseArmed = false;
    }
}

AnalogSensorEndpoint* IOModule::allocAnalogEndpoint_(const char* endpointId)
{
    if (analogEndpointPoolUsed_ >= MAX_ANALOG_ENDPOINTS) return nullptr;
    void* mem = analogEndpointPool_[analogEndpointPoolUsed_++];
    return new (mem) AnalogSensorEndpoint(endpointId);
}

DigitalSensorEndpoint* IOModule::allocDigitalSensorEndpoint_(const char* endpointId, uint8_t valueType)
{
    if (digitalSensorEndpointPoolUsed_ >= MAX_DIGITAL_INPUTS) return nullptr;
    void* mem = digitalSensorEndpointPool_[digitalSensorEndpointPoolUsed_++];
    return new (mem) DigitalSensorEndpoint(endpointId, valueType);
}

DigitalActuatorEndpoint* IOModule::allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx)
{
    if (digitalActuatorEndpointPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = digitalActuatorEndpointPool_[digitalActuatorEndpointPoolUsed_++];
    return new (mem) DigitalActuatorEndpoint(endpointId, writeFn, writeCtx);
}

IDigitalCounterDriver* IOModule::allocGpioDriver_(const char* driverId,
                                                  uint8_t pin,
                                                  bool output,
                                                  bool activeHigh,
                                                  uint8_t inputPullMode,
                                                  bool counterEnabled,
                                                  uint8_t edgeMode,
                                                  uint32_t counterDebounceUs)
{
    if (counterEnabled && !output) {
        if (gpioCounterDriverPoolUsed_ >= MAX_DIGITAL_INPUTS) return nullptr;
        void* mem = gpioCounterDriverPool_[gpioCounterDriverPoolUsed_++];
        return new (mem) GpioCounterDriver(driverId, pin, activeHigh, inputPullMode, edgeMode, counterDebounceUs);
    }

    if (gpioDriverPoolUsed_ >= MAX_DIGITAL_SLOTS) return nullptr;
    void* mem = gpioDriverPool_[gpioDriverPoolUsed_++];
    return new (mem) GpioDriver(driverId, pin, output, activeHigh, inputPullMode, false, 0);
}

IAnalogSourceDriver* IOModule::allocAdsDriver_(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg)
{
    if (adsDriverPoolUsed_ >= 2) return nullptr;
    void* mem = adsDriverPool_[adsDriverPoolUsed_++];
    return new (mem) Ads1115Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocDsDriver_(const char* driverId, OneWireBus* bus, const uint8_t address[8], const Ds18b20DriverConfig& cfg)
{
    if (dsDriverPoolUsed_ >= 2) return nullptr;
    void* mem = dsDriverPool_[dsDriverPoolUsed_++];
    return new (mem) Ds18b20Driver(driverId, bus, address, cfg);
}

IDigitalPinDriver* IOModule::allocPcfBitDriver_(const char* driverId, Pcf8574Driver* parent, uint8_t bit, bool activeHigh)
{
    if (pcfBitDriverPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = pcfBitDriverPool_[pcfBitDriverPoolUsed_++];
    return new (mem) Pcf8574BitDriver(driverId, parent, bit, activeHigh);
}

IMaskOutputDriver* IOModule::allocPcfDriver_(const char* driverId, I2CBus* bus, uint8_t address)
{
    if (pcfDriverPoolUsed_ >= 1) return nullptr;
    void* mem = pcfDriverPool_[pcfDriverPoolUsed_++];
    return new (mem) Pcf8574Driver(driverId, bus, address);
}

Pcf8574MaskEndpoint* IOModule::allocMaskEndpoint_(const char* endpointId, MaskWriteFn writeFn, MaskReadFn readFn, void* fnCtx)
{
    if (maskEndpointPoolUsed_ >= 1) return nullptr;
    void* mem = maskEndpointPool_[maskEndpointPoolUsed_++];
    return new (mem) Pcf8574MaskEndpoint(endpointId, writeFn, readFn, fnCtx);
}

bool IOModule::writeDigitalOut_(void* ctx, bool on)
{
    IOModule::DigitalSlot* s = static_cast<IOModule::DigitalSlot*>(ctx);
    if (!s || !s->provider.isBound()) return false;
    if (!s->used || s->kind != DIGITAL_SLOT_OUTPUT) return false;

    if (!s->outDef.momentary) {
        bool ok = s->provider.write(on);
        if (ok && s->owner) s->owner->markIoCycleChanged_(s->ioId);
        return ok;
    }

    // Momentary outputs always generate a physical pulse on each command.
    if (!s->provider.write(true)) return false;
    uint32_t pulse = (s->outDef.pulseMs == 0) ? 500u : (uint32_t)s->outDef.pulseMs;
    const uint32_t nowMs = millis();
    s->pulseDeadlineMs = nowMs + pulse;
    s->pulseArmed = true;
    if (s->owner) s->owner->markIoCycleChanged_(s->ioId);
    return true;
}

bool IOModule::endpointIndexFromId_(const char* id, uint8_t& idxOut) const
{
    if (!id || id[0] == '\0') return false;
    for (uint8_t i = 0; i < registry_.count(); ++i) {
        IOEndpoint* ep = registry_.at(i);
        if (!ep || !ep->id()) continue;
        if (strcmp(ep->id(), id) != 0) continue;
        idxOut = i;
        return true;
    }
    return false;
}

void IOModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Io;
    cfgStore_ = &cfg;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    if (!services.add(ServiceId::Io, &ioSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Io));
    }
    if (!services.add(ServiceId::StatusLeds, &statusLedsSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::StatusLeds));
    }

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(i2cSdaVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(i2cSclVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(adsPollVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(dsPollVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(digitalPollVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(adsInternalAddrVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(adsExternalAddrVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(adsGainVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(adsRateVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(pcfEnabledVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(pcfAddressVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(pcfMaskDefaultVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(pcfActiveLowVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(traceEnabledVar_, kCfgModuleId, kCfgBranchIoDebug);
    cfg.registerVar(tracePeriodVar_, kCfgModuleId, kCfgBranchIoDebug);

    cfg.registerVar(a0NameVar_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0BindingVar_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0C0Var_, kCfgModuleId, kCfgBranchIoA0);
    cfg.registerVar(a0C1Var_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0PrecVar_, kCfgModuleId, kCfgBranchIoA0);
    cfg.registerVar(a1NameVar_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1BindingVar_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1C0Var_, kCfgModuleId, kCfgBranchIoA1);
    cfg.registerVar(a1C1Var_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1PrecVar_, kCfgModuleId, kCfgBranchIoA1);
    cfg.registerVar(a2NameVar_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2BindingVar_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2C0Var_, kCfgModuleId, kCfgBranchIoA2);
    cfg.registerVar(a2C1Var_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2PrecVar_, kCfgModuleId, kCfgBranchIoA2);
    cfg.registerVar(a3NameVar_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3BindingVar_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3C0Var_, kCfgModuleId, kCfgBranchIoA3);
    cfg.registerVar(a3C1Var_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3PrecVar_, kCfgModuleId, kCfgBranchIoA3);
    cfg.registerVar(a4NameVar_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4BindingVar_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4C0Var_, kCfgModuleId, kCfgBranchIoA4);
    cfg.registerVar(a4C1Var_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4PrecVar_, kCfgModuleId, kCfgBranchIoA4);
    cfg.registerVar(a5NameVar_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5BindingVar_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5C0Var_, kCfgModuleId, kCfgBranchIoA5);
    cfg.registerVar(a5C1Var_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5PrecVar_, kCfgModuleId, kCfgBranchIoA5);

    cfg.registerVar(i0NameVar_, kCfgModuleId, kCfgBranchIoI0); cfg.registerVar(i0BindingVar_, kCfgModuleId, kCfgBranchIoI0); cfg.registerVar(i0ActiveHighVar_, kCfgModuleId, kCfgBranchIoI0); cfg.registerVar(i0PullModeVar_, kCfgModuleId, kCfgBranchIoI0); cfg.registerVar(i0EdgeModeVar_, kCfgModuleId, kCfgBranchIoI0);
    cfg.registerVar(i1NameVar_, kCfgModuleId, kCfgBranchIoI1); cfg.registerVar(i1BindingVar_, kCfgModuleId, kCfgBranchIoI1); cfg.registerVar(i1ActiveHighVar_, kCfgModuleId, kCfgBranchIoI1); cfg.registerVar(i1PullModeVar_, kCfgModuleId, kCfgBranchIoI1); cfg.registerVar(i1EdgeModeVar_, kCfgModuleId, kCfgBranchIoI1);
    cfg.registerVar(i2NameVar_, kCfgModuleId, kCfgBranchIoI2); cfg.registerVar(i2BindingVar_, kCfgModuleId, kCfgBranchIoI2); cfg.registerVar(i2ActiveHighVar_, kCfgModuleId, kCfgBranchIoI2); cfg.registerVar(i2PullModeVar_, kCfgModuleId, kCfgBranchIoI2); cfg.registerVar(i2EdgeModeVar_, kCfgModuleId, kCfgBranchIoI2);
    cfg.registerVar(i3NameVar_, kCfgModuleId, kCfgBranchIoI3); cfg.registerVar(i3BindingVar_, kCfgModuleId, kCfgBranchIoI3); cfg.registerVar(i3ActiveHighVar_, kCfgModuleId, kCfgBranchIoI3); cfg.registerVar(i3PullModeVar_, kCfgModuleId, kCfgBranchIoI3); cfg.registerVar(i3EdgeModeVar_, kCfgModuleId, kCfgBranchIoI3);
    cfg.registerVar(i4NameVar_, kCfgModuleId, kCfgBranchIoI4); cfg.registerVar(i4BindingVar_, kCfgModuleId, kCfgBranchIoI4); cfg.registerVar(i4ActiveHighVar_, kCfgModuleId, kCfgBranchIoI4); cfg.registerVar(i4PullModeVar_, kCfgModuleId, kCfgBranchIoI4); cfg.registerVar(i4EdgeModeVar_, kCfgModuleId, kCfgBranchIoI4);

    cfg.registerVar(d0NameVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0BindingVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0ActiveHighVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0InitialOnVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0MomentaryVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0PulseVar_, kCfgModuleId, kCfgBranchIoD0);
    cfg.registerVar(d1NameVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1BindingVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1ActiveHighVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1InitialOnVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1MomentaryVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1PulseVar_, kCfgModuleId, kCfgBranchIoD1);
    cfg.registerVar(d2NameVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2BindingVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2ActiveHighVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2InitialOnVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2MomentaryVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2PulseVar_, kCfgModuleId, kCfgBranchIoD2);
    cfg.registerVar(d3NameVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3BindingVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3ActiveHighVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3InitialOnVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3MomentaryVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3PulseVar_, kCfgModuleId, kCfgBranchIoD3);
    cfg.registerVar(d4NameVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4BindingVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4ActiveHighVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4InitialOnVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4MomentaryVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4PulseVar_, kCfgModuleId, kCfgBranchIoD4);
    cfg.registerVar(d5NameVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5BindingVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5ActiveHighVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5InitialOnVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5MomentaryVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5PulseVar_, kCfgModuleId, kCfgBranchIoD5);
    cfg.registerVar(d6NameVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6BindingVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6ActiveHighVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6InitialOnVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6MomentaryVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6PulseVar_, kCfgModuleId, kCfgBranchIoD6);
    cfg.registerVar(d7NameVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7BindingVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7ActiveHighVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7InitialOnVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7MomentaryVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7PulseVar_, kCfgModuleId, kCfgBranchIoD7);

    LOGI("I/O config registered");
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        analogPrecisionLast_[i] = sanitizeAnalogPrecision_(analogCfg_[i].precision);
    }
    analogPrecisionLastInit_ = true;
    analogConfigDirtyMask_ = 0;

    (void)logHub_;
}

void IOModule::onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;
    if (!cfgMqttPubConfigured_) {
        cfgMqttPub_.configure(this,
                              kIoCfgProducerId,
                              kIoCfgRoutes,
                              (uint8_t)(sizeof(kIoCfgRoutes) / sizeof(kIoCfgRoutes[0])),
                              services);
        cfgMqttPubConfigured_ = true;
    }

    LOGI("io.onConfigLoaded begin enabled=%s i2c_sda=%ld i2c_scl=%ld runtimeReady=%s",
         cfgData_.enabled ? "true" : "false",
         (long)cfgData_.i2cSda,
         (long)cfgData_.i2cScl,
         runtimeReady_ ? "true" : "false");

    // Allocate and wire all IO runtime objects once after persistent config is loaded.
    runtimeInitAttempted_ = true;
    if (cfgData_.enabled) {
        runtimeReady_ = configureRuntime_();
        if (!runtimeReady_) {
            LOGW("Runtime init failed during io.onConfigLoaded; no runtime allocations will be attempted later");
        } else {
            LOGI("io.onConfigLoaded runtime configured");
        }
    } else {
        runtimeReady_ = false;
        LOGI("io.onConfigLoaded skipped runtime init (disabled)");
    }
}

void IOModule::loop()
{
    const IoStatus st = ioTick_(millis());
    if (st != IO_OK) {
        if (!cfgData_.enabled || !runtimeReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
