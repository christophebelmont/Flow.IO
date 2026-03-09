/**
 * @file IOModule.cpp
 * @brief Implementation file.
 */

#include "IOModule.h"
#include "Core/MqttTopics.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::IOModule)
#include "Core/ModuleLog.h"
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
        analogCfg_[analogIdx].source = def.source;
        analogCfg_[analogIdx].channel = def.channel;
        analogCfg_[analogIdx].c0 = def.c0;
        analogCfg_[analogIdx].c1 = def.c1;
        analogCfg_[analogIdx].precision = def.precision;
        analogCfg_[analogIdx].minValid = def.minValid;
        analogCfg_[analogIdx].maxValid = def.maxValid;
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
    if (def.pin == 0) return false;
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
        return true;
    }

    return false;
}

bool IOModule::defineDigitalOutput(const IODigitalOutputDefinition& def)
{
    if (def.id[0] == '\0') return false;
    if (def.pin == 0) return false;
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
            digitalCfg_[cfgIdx].pin = def.pin;
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

    if (self->adsInternal_) self->adsInternal_->tick(nowMs);
    if (self->adsExternal_) self->adsExternal_->tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].def.source;
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

    if (self->dsWater_) self->dsWater_->tick(nowMs);
    if (self->dsAir_) self->dsAir_->tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].def.source;
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

bool IOModule::processAnalogDefinition_(uint8_t idx, uint32_t nowMs)
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    AnalogSlot& slot = analogSlots_[idx];
    if (!slot.used || !slot.endpoint) return false;

    IAnalogSourceDriver* sourceDriver = nullptr;
    if (slot.def.source == IO_SRC_ADS_INTERNAL_SINGLE) sourceDriver = adsInternal_;
    else if (slot.def.source == IO_SRC_ADS_EXTERNAL_DIFF) sourceDriver = adsExternal_;
    else if (slot.def.source == IO_SRC_DS18_WATER) sourceDriver = dsWater_;
    else if (slot.def.source == IO_SRC_DS18_AIR) sourceDriver = dsAir_;
    if (!sourceDriver) return false;

    IOAnalogSample sample{};
    if (!sourceDriver->readSample(slot.def.channel, sample)) {
        const bool isDsSource =
            (slot.def.source == IO_SRC_DS18_WATER) || (slot.def.source == IO_SRC_DS18_AIR);
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

    if (raw < slot.def.minValid || raw > slot.def.maxValid) {
        // Transition to invalid only once; avoid timestamp churn while value stays unavailable.
        if (slot.lastRoundedValid) {
            slot.endpoint->update(raw, false, nowMs);
            slot.lastRoundedValid = false;
        }
        return false;
    }

    float filtered = slot.median.update(raw);
    float calibrated = (slot.def.c0 * filtered) + slot.def.c1;
    float rounded = ioRoundToPrecision(calibrated, slot.def.precision);

    // Trace pH/ORP/PSI calculation chain with configurable periodic ticker.
    bool isAdsSource = (slot.def.source == IO_SRC_ADS_INTERNAL_SINGLE) ||
                       (slot.def.source == IO_SRC_ADS_EXTERNAL_DIFF);
    if (cfgData_.traceEnabled && isAdsSource && idx < 3) {
        uint32_t periodMs =
            (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : Limits::IoTracePeriodMs;
        uint32_t& lastMs = analogCalcLogLastMs_[idx];
        if (lastMs == 0U || (uint32_t)(nowMs - lastMs) >= periodMs) {
            const char* sensor = (idx == 0) ? "ORP" : ((idx == 1) ? "pH" : "PSI");
            const char sourceMark = (slot.def.source == IO_SRC_ADS_INTERNAL_SINGLE) ? 'I' : 'E';
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
    if (!slot.used || slot.kind != DIGITAL_SLOT_INPUT || !slot.driver || !slot.endpoint) return false;
    if (slot.endpoint->type() != IO_EP_DIGITAL_SENSOR) return false;

    DigitalSensorEndpoint* inputEp = static_cast<DigitalSensorEndpoint*>(slot.endpoint);

    bool on = false;
    if (!slot.driver->read(on)) {
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
        markIoCycleChanged_(slot.ioId);
        if (slot.inDef.onValueChanged) {
            slot.inDef.onValueChanged(slot.inDef.onValueCtx, on);
        }
    }

    return true;
}

int32_t IOModule::clampPrecisionForHa_(int32_t precision) const
{
    if (precision < 0) return 0;
    if (precision > 6) return 6;
    return precision;
}

void IOModule::buildHaValueTemplate_(uint8_t analogIdx, char* out, size_t outLen) const
{
    if (!out || outLen == 0 || analogIdx >= ANALOG_CFG_SLOTS) return;
    int32_t p = clampPrecisionForHa_(analogCfg_[analogIdx].precision);
    snprintf(
        out,
        outLen,
        "{%% if value_json.value is number %%}{{ value_json.value | float | round(%ld) }}{%% else %%}unavailable{%% endif %%}",
        (long)p
    );
}

void IOModule::registerHaAnalogSensors_()
{
    if (!haSvc_ || !haSvc_->addSensor) return;
    static constexpr const char* kIoValueAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";

    buildHaValueTemplate_(0, haValueTpl_[0], sizeof(haValueTpl_[0]));
    buildHaValueTemplate_(1, haValueTpl_[1], sizeof(haValueTpl_[1]));
    buildHaValueTemplate_(2, haValueTpl_[2], sizeof(haValueTpl_[2]));
    buildHaValueTemplate_(3, haValueTpl_[3], sizeof(haValueTpl_[3]));
    buildHaValueTemplate_(4, haValueTpl_[4], sizeof(haValueTpl_[4]));
    buildHaValueTemplate_(5, haValueTpl_[5], sizeof(haValueTpl_[5]));

    const HASensorEntry s0{"io", "orp", "ORP", "rt/io/input/a0", haValueTpl_[0], nullptr, "mdi:flash", "mV", false, kIoValueAvailabilityTpl};
    const HASensorEntry s1{"io", "ph", "pH", "rt/io/input/a1", haValueTpl_[1], nullptr, "mdi:ph", "", false, kIoValueAvailabilityTpl};
    const HASensorEntry s2{"io", "psi", "PSI", "rt/io/input/a2", haValueTpl_[2], nullptr, "mdi:gauge", "PSI", false, kIoValueAvailabilityTpl};
    const HASensorEntry s3{"io", "spare", "Spare", "rt/io/input/a3", haValueTpl_[3], nullptr, "mdi:sine-wave", nullptr, false, kIoValueAvailabilityTpl};
    const HASensorEntry s4{"io", "water_temperature", "Water Temperature", "rt/io/input/a4", haValueTpl_[4], nullptr, "mdi:water-thermometer", "\xC2\xB0""C", false, kIoValueAvailabilityTpl};
    const HASensorEntry s5{"io", "air_temperature", "Air Temperature", "rt/io/input/a5", haValueTpl_[5], nullptr, "mdi:thermometer", "\xC2\xB0""C", false, kIoValueAvailabilityTpl};
    (void)haSvc_->addSensor(haSvc_->ctx, &s0);
    (void)haSvc_->addSensor(haSvc_->ctx, &s1);
    (void)haSvc_->addSensor(haSvc_->ctx, &s2);
    (void)haSvc_->addSensor(haSvc_->ctx, &s3);
    (void)haSvc_->addSensor(haSvc_->ctx, &s4);
    (void)haSvc_->addSensor(haSvc_->ctx, &s5);
}

void IOModule::registerHaDigitalInputBinarySensors_()
{
    if (!haSvc_ || !haSvc_->addBinarySensor) return;

    static constexpr const char* kBoolTpl = "{{ 'True' if value_json.value else 'False' }}";
    const HABinarySensorEntry poolLevel{
        "io",
        "pool_level",
        "Pool Level",
        "rt/io/input/i0",
        kBoolTpl,
        nullptr,
        nullptr,
        "mdi:waves-arrow-up"
    };
    const HABinarySensorEntry phLevel{
        "io",
        "ph_level",
        "pH Level",
        "rt/io/input/i1",
        kBoolTpl,
        nullptr,
        nullptr,
        "mdi:flask-outline"
    };
    const HABinarySensorEntry chlorineLevel{
        "io",
        "chlorine_level",
        "Chlorine Level",
        "rt/io/input/i2",
        kBoolTpl,
        nullptr,
        nullptr,
        "mdi:test-tube"
    };

    (void)haSvc_->addBinarySensor(haSvc_->ctx, &poolLevel);
    (void)haSvc_->addBinarySensor(haSvc_->ctx, &phLevel);
    (void)haSvc_->addBinarySensor(haSvc_->ctx, &chlorineLevel);
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

void IOModule::maybeRefreshHaOnPrecisionChange_()
{
    if (!haPrecisionLastInit_) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            int32_t p = clampPrecisionForHa_(analogCfg_[i].precision);
            haPrecisionLast_[i] = p;
        }
        haPrecisionLastInit_ = true;
        return;
    }

    bool changed = false;
    uint16_t changedMask = 0;
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        int32_t p = clampPrecisionForHa_(analogCfg_[i].precision);
        if (haPrecisionLast_[i] == p) continue;
        haPrecisionLast_[i] = p;
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
        if (haSvc_ && haSvc_->addSensor) {
            LOGI("Input precision changed -> request HA discovery refresh");
            registerHaAnalogSensors_();
            if (haSvc_->requestRefresh) {
                (void)haSvc_->requestRefresh(haSvc_->ctx);
            }
        }
    }
}

uint8_t IOModule::svcCount_(void* ctx)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    return self ? self->ioCount_() : 0;
}

IoStatus IOModule::svcIdAt_(void* ctx, uint8_t index, IoId* outId)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self) return IO_ERR_INVALID_ARG;
    return self->ioIdAt_(index, outId);
}

IoStatus IOModule::svcMeta_(void* ctx, IoId id, IoEndpointMeta* outMeta)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self) return IO_ERR_INVALID_ARG;
    return self->ioMeta_(id, outMeta);
}

IoStatus IOModule::svcReadDigital_(void* ctx, IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self) return IO_ERR_INVALID_ARG;
    return self->ioReadDigital_(id, outOn, outTsMs, outSeq);
}

IoStatus IOModule::svcWriteDigital_(void* ctx, IoId id, uint8_t on, uint32_t tsMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self) return IO_ERR_INVALID_ARG;
    return self->ioWriteDigital_(id, on, tsMs);
}

IoStatus IOModule::svcReadAnalog_(void* ctx, IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self) return IO_ERR_INVALID_ARG;
    return self->ioReadAnalog_(id, outValue, outTsMs, outSeq);
}

IoStatus IOModule::svcTick_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self) return IO_ERR_INVALID_ARG;
    return self->ioTick_(nowMs);
}

IoStatus IOModule::svcLastCycle_(void* ctx, IoCycleInfo* outCycle)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self) return IO_ERR_INVALID_ARG;
    return self->ioLastCycle_(outCycle);
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
        outMeta->backend = IO_BACKEND_GPIO;
        outMeta->channel = (s.kind == DIGITAL_SLOT_OUTPUT) ? s.outDef.pin : s.inDef.pin;
        outMeta->capabilities = (s.kind == DIGITAL_SLOT_OUTPUT) ? (IO_CAP_R | IO_CAP_W) : IO_CAP_R;

        const char* name = nullptr;
        if (s.kind == DIGITAL_SLOT_OUTPUT && s.logicalIdx < DIGITAL_CFG_SLOTS) {
            name = digitalCfg_[s.logicalIdx].name;
        } else if (s.kind == DIGITAL_SLOT_INPUT) {
            name = s.inDef.id;
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
        outMeta->capabilities = IO_CAP_R;
        outMeta->channel = s.def.channel;
        if (s.def.source == IO_SRC_ADS_INTERNAL_SINGLE) outMeta->backend = IO_BACKEND_ADS1115_INT;
        else if (s.def.source == IO_SRC_ADS_EXTERNAL_DIFF) outMeta->backend = IO_BACKEND_ADS1115_EXT_DIFF;
        else outMeta->backend = IO_BACKEND_DS18B20;
        outMeta->precision = s.def.precision;
        outMeta->minValid = s.def.minValid;
        outMeta->maxValid = s.def.maxValid;

        const char* name = (analogIdx < ANALOG_CFG_SLOTS) ? analogCfg_[analogIdx].name : nullptr;
        if (!name || name[0] == '\0') name = s.def.id;
        if (!name) name = "";
        strncpy(outMeta->name, name, sizeof(outMeta->name) - 1);
        outMeta->name[sizeof(outMeta->name) - 1] = '\0';
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
    if (!s.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_BOOL) return IO_ERR_NOT_READY;

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
    maybeRefreshHaOnPrecisionChange_();

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

bool IOModule::svcSetMask_(void* ctx, uint8_t mask)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    return self ? self->setLedMask_(mask, millis()) : false;
}

bool IOModule::svcTurnOn_(void* ctx, uint8_t bit)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    return self ? self->turnLedOn_(bit, millis()) : false;
}

bool IOModule::svcTurnOff_(void* ctx, uint8_t bit)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    return self ? self->turnLedOff_(bit, millis()) : false;
}

bool IOModule::svcGetMask_(void* ctx, uint8_t* mask)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !mask) return false;
    return self->getLedMask_(*mask);
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

bool IOModule::configureRuntime_()
{
    if (runtimeReady_) return true;
    if (!cfgData_.enabled) return false;

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

        if (i < ANALOG_CFG_SLOTS) {
            snprintf(analogSlots_[i].def.id, sizeof(analogSlots_[i].def.id), "a%u", (unsigned)i);
            analogSlots_[i].def.source = analogCfg_[i].source;
            analogSlots_[i].def.channel = analogCfg_[i].channel;
            analogSlots_[i].def.c0 = analogCfg_[i].c0;
            analogSlots_[i].def.c1 = analogCfg_[i].c1;
            analogSlots_[i].def.precision = analogCfg_[i].precision;
            analogSlots_[i].def.minValid = analogCfg_[i].minValid;
            analogSlots_[i].def.maxValid = analogCfg_[i].maxValid;

            if (i < 3) {
                LOGI("Analog map %s source=%u channel=%u", analogSlots_[i].def.id,
                     (unsigned)analogSlots_[i].def.source,
                     (unsigned)analogSlots_[i].def.channel);
            }
        }

        if (analogSlots_[i].def.source == IO_SRC_ADS_INTERNAL_SINGLE) needAdsInternal = true;
        else if (analogSlots_[i].def.source == IO_SRC_ADS_EXTERNAL_DIFF) needAdsExternal = true;
        else if (analogSlots_[i].def.source == IO_SRC_DS18_WATER) needDsWater = true;
        else if (analogSlots_[i].def.source == IO_SRC_DS18_AIR) needDsAir = true;

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
            snprintf(s.endpointId, sizeof(s.endpointId), "i%u", (unsigned)s.logicalIdx);
            s.driver = allocGpioDriver_(
                s.endpointId,
                s.inDef.pin,
                false,
                s.inDef.activeHigh,
                s.inDef.pullMode
            );
            if (!s.driver) continue;
            if (!s.driver->begin()) continue;

            s.endpoint = allocDigitalSensorEndpoint_(s.endpointId);
            if (!s.endpoint) continue;
            registry_.add(s.endpoint);
            (void)processDigitalInputDefinition_(i, millis());
            continue;
        }

        const uint8_t cfgIdx = s.logicalIdx;
        if (cfgIdx < DIGITAL_CFG_SLOTS) {
            snprintf(s.outDef.id, sizeof(s.outDef.id), "d%u", (unsigned)cfgIdx);
            if (digitalCfg_[cfgIdx].pin != 0) s.outDef.pin = digitalCfg_[cfgIdx].pin;
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

        s.driver = allocGpioDriver_(s.outDef.id, s.outDef.pin, true, s.outDef.activeHigh);
        if (!s.driver) continue;
        if (!s.driver->begin()) continue;
        s.driver->write(s.outDef.initialOn);
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
        adsInternal_ = allocAdsDriver_("ads_internal", &i2cBus_, adsInternalCfg);
        if (!adsInternal_) {
            LOGW("ADS internal pool exhausted");
        } else
        if (!adsInternal_->begin()) {
            LOGW("ADS internal not detected at 0x%02X", cfgData_.adsInternalAddr);
            adsInternal_ = nullptr;
        } else
        if (cfgData_.adsInternalAddr == 0x49) {
            LOGI("ADS1115 found at 0x49 (internal)");
        }
    }

    if (needAdsExternal) {
        adsExternal_ = allocAdsDriver_("ads_external", &i2cBus_, adsExternalCfg);
        if (!adsExternal_) {
            LOGW("ADS external pool exhausted");
        } else
        if (!adsExternal_->begin()) {
            LOGW("ADS external not detected at 0x%02X", cfgData_.adsExternalAddr);
            adsExternal_ = nullptr;
        } else
        if (cfgData_.adsExternalAddr == 0x49) {
            LOGI("ADS1115 found at 0x49 (external)");
        }
    }

    Ds18b20DriverConfig dsCfg{};
    dsCfg.pollMs = (cfgData_.dsPollMs < 750) ? 750 : (uint32_t)cfgData_.dsPollMs;
    dsCfg.conversionWaitMs = 750;

    if (needDsWater && oneWireWater_) {
        oneWireWater_->begin();
        if (oneWireWater_->getAddress(0, oneWireWaterAddr_)) {
            dsWater_ = allocDsDriver_("ds18_water", oneWireWater_, oneWireWaterAddr_, dsCfg);
            if (dsWater_) dsWater_->begin();
            else LOGW("DS18 water pool exhausted");
        } else {
            LOGW("No DS18B20 found on water OneWire bus");
        }
    }

    if (needDsAir && oneWireAir_) {
        oneWireAir_->begin();
        if (oneWireAir_->getAddress(0, oneWireAirAddr_)) {
            dsAir_ = allocDsDriver_("ds18_air", oneWireAir_, oneWireAirAddr_, dsCfg);
            if (dsAir_) dsAir_->begin();
            else LOGW("DS18 air pool exhausted");
        } else {
            LOGW("No DS18B20 found on air OneWire bus");
        }
    }

    if (cfgData_.pcfEnabled) {
        pcf_ = allocPcfDriver_("pcf8574_led", &i2cBus_, cfgData_.pcfAddress);
        if (!pcf_) {
            LOGW("PCF8574 pool exhausted");
        } else
        if (pcf_->begin()) {
            ledMaskEp_ = allocMaskEndpoint_(
                "status_leds_mask",
                [](void* ctx, uint8_t mask) -> bool {
                    return static_cast<IMaskOutputDriver*>(ctx)->writeMask(mask);
                },
                [](void* ctx, uint8_t* mask) -> bool {
                    if (!mask) return false;
                    return static_cast<IMaskOutputDriver*>(ctx)->readMask(*mask);
                },
                pcf_
            );
            if (ledMaskEp_) {
                registry_.add(ledMaskEp_);
                setLedMask_(cfgData_.pcfMaskDefault, millis());
            }
        } else {
            LOGW("PCF8574 not detected at 0x%02X", cfgData_.pcfAddress);
            pcf_ = nullptr;
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
        if (!s.outDef.momentary || !s.pulseArmed || !s.driver) continue;
        if ((int32_t)(nowMs - s.pulseDeadlineMs) < 0) continue;
        (void)s.driver->write(false);
        s.pulseArmed = false;
    }
}

AnalogSensorEndpoint* IOModule::allocAnalogEndpoint_(const char* endpointId)
{
    if (analogEndpointPoolUsed_ >= MAX_ANALOG_ENDPOINTS) return nullptr;
    void* mem = analogEndpointPool_[analogEndpointPoolUsed_++];
    return new (mem) AnalogSensorEndpoint(endpointId);
}

DigitalSensorEndpoint* IOModule::allocDigitalSensorEndpoint_(const char* endpointId)
{
    if (digitalSensorEndpointPoolUsed_ >= MAX_DIGITAL_INPUTS) return nullptr;
    void* mem = digitalSensorEndpointPool_[digitalSensorEndpointPoolUsed_++];
    return new (mem) DigitalSensorEndpoint(endpointId);
}

DigitalActuatorEndpoint* IOModule::allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx)
{
    if (digitalActuatorEndpointPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = digitalActuatorEndpointPool_[digitalActuatorEndpointPoolUsed_++];
    return new (mem) DigitalActuatorEndpoint(endpointId, writeFn, writeCtx);
}

IDigitalPinDriver* IOModule::allocGpioDriver_(const char* driverId, uint8_t pin, bool output, bool activeHigh, uint8_t inputPullMode)
{
    if (gpioDriverPoolUsed_ >= MAX_DIGITAL_SLOTS) return nullptr;
    void* mem = gpioDriverPool_[gpioDriverPoolUsed_++];
    return new (mem) GpioDriver(driverId, pin, output, activeHigh, inputPullMode);
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
    if (!s || !s->driver) return false;
    if (!s->used || s->kind != DIGITAL_SLOT_OUTPUT) return false;

    if (!s->outDef.momentary) {
        bool ok = s->driver->write(on);
        if (ok && s->owner) s->owner->markIoCycleChanged_(s->ioId);
        return ok;
    }

    // Momentary outputs always generate a physical pulse on each command.
    if (!s->driver->write(true)) return false;
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
    logHub_ = services.get<LogHubService>("loghub");
    haSvc_ = services.get<HAService>("ha");
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    (void)services.add("io", &ioSvc_);

    // Default labels for digital output slots (can be overridden by persisted config).
    for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
        const PoolIoBinding& b = FLOW_POOL_IO_BINDINGS[i];
        if (b.ioId < IO_ID_DO_BASE) continue;
        const uint8_t logical = (uint8_t)(b.ioId - IO_ID_DO_BASE);
        if (logical >= DIGITAL_CFG_SLOTS) continue;
        snprintf(digitalCfg_[logical].name, sizeof(digitalCfg_[logical].name), "%s", b.name ? b.name : "");
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

    cfg.registerVar(a0NameVar_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0SourceVar_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0ChannelVar_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0C0Var_, kCfgModuleId, kCfgBranchIoA0);
    cfg.registerVar(a0C1Var_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0PrecVar_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0MinVar_, kCfgModuleId, kCfgBranchIoA0); cfg.registerVar(a0MaxVar_, kCfgModuleId, kCfgBranchIoA0);

    cfg.registerVar(a1NameVar_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1SourceVar_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1ChannelVar_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1C0Var_, kCfgModuleId, kCfgBranchIoA1);
    cfg.registerVar(a1C1Var_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1PrecVar_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1MinVar_, kCfgModuleId, kCfgBranchIoA1); cfg.registerVar(a1MaxVar_, kCfgModuleId, kCfgBranchIoA1);

    cfg.registerVar(a2NameVar_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2SourceVar_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2ChannelVar_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2C0Var_, kCfgModuleId, kCfgBranchIoA2);
    cfg.registerVar(a2C1Var_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2PrecVar_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2MinVar_, kCfgModuleId, kCfgBranchIoA2); cfg.registerVar(a2MaxVar_, kCfgModuleId, kCfgBranchIoA2);

    cfg.registerVar(a3NameVar_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3SourceVar_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3ChannelVar_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3C0Var_, kCfgModuleId, kCfgBranchIoA3);
    cfg.registerVar(a3C1Var_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3PrecVar_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3MinVar_, kCfgModuleId, kCfgBranchIoA3); cfg.registerVar(a3MaxVar_, kCfgModuleId, kCfgBranchIoA3);

    cfg.registerVar(a4NameVar_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4SourceVar_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4ChannelVar_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4C0Var_, kCfgModuleId, kCfgBranchIoA4);
    cfg.registerVar(a4C1Var_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4PrecVar_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4MinVar_, kCfgModuleId, kCfgBranchIoA4); cfg.registerVar(a4MaxVar_, kCfgModuleId, kCfgBranchIoA4);
    cfg.registerVar(a5NameVar_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5SourceVar_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5ChannelVar_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5C0Var_, kCfgModuleId, kCfgBranchIoA5);
    cfg.registerVar(a5C1Var_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5PrecVar_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5MinVar_, kCfgModuleId, kCfgBranchIoA5); cfg.registerVar(a5MaxVar_, kCfgModuleId, kCfgBranchIoA5);

    cfg.registerVar(d0NameVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0PinVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0ActiveHighVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0InitialOnVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0MomentaryVar_, kCfgModuleId, kCfgBranchIoD0); cfg.registerVar(d0PulseVar_, kCfgModuleId, kCfgBranchIoD0);
    cfg.registerVar(d1NameVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1PinVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1ActiveHighVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1InitialOnVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1MomentaryVar_, kCfgModuleId, kCfgBranchIoD1); cfg.registerVar(d1PulseVar_, kCfgModuleId, kCfgBranchIoD1);
    cfg.registerVar(d2NameVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2PinVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2ActiveHighVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2InitialOnVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2MomentaryVar_, kCfgModuleId, kCfgBranchIoD2); cfg.registerVar(d2PulseVar_, kCfgModuleId, kCfgBranchIoD2);
    cfg.registerVar(d3NameVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3PinVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3ActiveHighVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3InitialOnVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3MomentaryVar_, kCfgModuleId, kCfgBranchIoD3); cfg.registerVar(d3PulseVar_, kCfgModuleId, kCfgBranchIoD3);
    cfg.registerVar(d4NameVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4PinVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4ActiveHighVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4InitialOnVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4MomentaryVar_, kCfgModuleId, kCfgBranchIoD4); cfg.registerVar(d4PulseVar_, kCfgModuleId, kCfgBranchIoD4);
    cfg.registerVar(d5NameVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5PinVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5ActiveHighVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5InitialOnVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5MomentaryVar_, kCfgModuleId, kCfgBranchIoD5); cfg.registerVar(d5PulseVar_, kCfgModuleId, kCfgBranchIoD5);
    cfg.registerVar(d6NameVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6PinVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6ActiveHighVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6InitialOnVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6MomentaryVar_, kCfgModuleId, kCfgBranchIoD6); cfg.registerVar(d6PulseVar_, kCfgModuleId, kCfgBranchIoD6);
    cfg.registerVar(d7NameVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7PinVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7ActiveHighVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7InitialOnVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7MomentaryVar_, kCfgModuleId, kCfgBranchIoD7); cfg.registerVar(d7PulseVar_, kCfgModuleId, kCfgBranchIoD7);

    LOGI("I/O config registered");
    if (haSvc_) {
        if (haSvc_->addSensor) registerHaAnalogSensors_();
        if (haSvc_->addBinarySensor) registerHaDigitalInputBinarySensors_();
    }
    if (haSvc_ && haSvc_->addSwitch) {
        for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
            const PoolIoBinding& b = FLOW_POOL_IO_BINDINGS[i];
            if (b.ioId < IO_ID_DO_BASE) continue;
            const uint8_t logical = (uint8_t)(b.ioId - IO_ID_DO_BASE);
            if (logical >= MAX_DIGITAL_OUTPUTS) continue;

            snprintf(haSwitchStateSuffix_[i], sizeof(haSwitchStateSuffix_[i]), "rt/io/output/d%u", (unsigned)logical);
            bool payloadOk = true;
            if (b.slot == 0) {
                int wrote = snprintf(
                    haSwitchPayloadOn_[i],
                    sizeof(haSwitchPayloadOn_[i]),
                    "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":true}}"
                );
                if (!(wrote > 0 && wrote < (int)sizeof(haSwitchPayloadOn_[i]))) payloadOk = false;
                wrote = snprintf(
                    haSwitchPayloadOff_[i],
                    sizeof(haSwitchPayloadOff_[i]),
                    "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":false}}"
                );
                if (!(wrote > 0 && wrote < (int)sizeof(haSwitchPayloadOff_[i]))) payloadOk = false;
            } else {
                int wrote = snprintf(
                    haSwitchPayloadOn_[i],
                    sizeof(haSwitchPayloadOn_[i]),
                    "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":true}}",
                    (unsigned)b.slot
                );
                if (!(wrote > 0 && wrote < (int)sizeof(haSwitchPayloadOn_[i]))) payloadOk = false;
                wrote = snprintf(
                    haSwitchPayloadOff_[i],
                    sizeof(haSwitchPayloadOff_[i]),
                    "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":false}}",
                    (unsigned)b.slot
                );
                if (!(wrote > 0 && wrote < (int)sizeof(haSwitchPayloadOff_[i]))) payloadOk = false;
            }
            if (!payloadOk) {
                LOGW("HA switch command payload truncated slot=%u", (unsigned)b.slot);
                continue;
            }

            const HASwitchEntry sw{
                "io",
                b.haObjectSuffix,
                b.name,
                haSwitchStateSuffix_[i],
                "{% if value_json.value %}ON{% else %}OFF{% endif %}",
                MqttTopics::SuffixCmd,
                haSwitchPayloadOn_[i],
                haSwitchPayloadOff_[i],
                b.haIcon,
                nullptr
            };
            (void)haSvc_->addSwitch(haSvc_->ctx, &sw);
        }
    }
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        haPrecisionLast_[i] = clampPrecisionForHa_(analogCfg_[i].precision);
    }
    haPrecisionLastInit_ = true;

    (void)logHub_;
}

void IOModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kIoCfgProducerId,
                               kIoCfgRoutes,
                               (uint8_t)(sizeof(kIoCfgRoutes) / sizeof(kIoCfgRoutes[0])),
                               services);
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
