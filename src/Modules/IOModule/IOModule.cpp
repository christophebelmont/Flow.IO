/**
 * @file IOModule.cpp
 * @brief Implementation file.
 */

#include "IOModule.h"
#define LOG_TAG "IOModule"
#include "Core/ModuleLog.h"
#include "Modules/IOModule/IORuntime.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

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
    int wrote = snprintf(out, len, "{\"id\":\"%s\",\"name\":\"%s\",\"value\":",
                         (id && id[0] != '\0') ? id : "",
                         (label && label[0] != '\0') ? label : ((id && id[0] != '\0') ? id : ""));
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
        wrote = snprintf(out + used, len - used, "%s\"%s\":{\"name\":\"%s\",\"value\":",
                         first ? "" : ",",
                         id,
                         (label && label[0] != '\0') ? label : id);
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
    return true;
}

bool IOModule::processAnalogDefinition_(uint8_t idx, uint32_t nowMs)
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    AnalogSlot& slot = analogSlots_[idx];
    if (!slot.used || !slot.endpoint) return false;

    float raw = 0.0f;
    int16_t rawBinary = 0;
    uint32_t sampleSeq = 0;
    bool hasSampleSeq = false;
    bool valid = false;

    if (slot.def.source == IO_SRC_ADS_INTERNAL_SINGLE) {
        if (!adsInternal_) return false;
        bool rawOk = adsInternal_->readRawChannel(slot.def.channel, rawBinary);
        bool vOk = adsInternal_->readVoltsChannel(slot.def.channel, raw);
        bool seqOk = adsInternal_->readSampleSeqChannel(slot.def.channel, sampleSeq);
        hasSampleSeq = true;
        valid = rawOk && vOk && seqOk;
    } else if (slot.def.source == IO_SRC_ADS_EXTERNAL_DIFF) {
        if (!adsExternal_) return false;
        if (slot.def.channel == 0) {
            bool rawOk = adsExternal_->readRawDifferential01(rawBinary);
            bool vOk = adsExternal_->readVoltsDifferential01(raw);
            bool seqOk = adsExternal_->readSampleSeqDifferential01(sampleSeq);
            hasSampleSeq = true;
            valid = rawOk && vOk && seqOk;
        } else {
            bool rawOk = adsExternal_->readRawDifferential23(rawBinary);
            bool vOk = adsExternal_->readVoltsDifferential23(raw);
            bool seqOk = adsExternal_->readSampleSeqDifferential23(sampleSeq);
            hasSampleSeq = true;
            valid = rawOk && vOk && seqOk;
        }
    } else if (slot.def.source == IO_SRC_DS18_WATER) {
        if (!dsWater_) return false;
        valid = dsWater_->readCelsius(raw);
    } else if (slot.def.source == IO_SRC_DS18_AIR) {
        if (!dsAir_) return false;
        valid = dsAir_->readCelsius(raw);
    }

    if (!valid) return false;

    // ADS values are processed only when a fresh sample arrives for that channel/pair.
    if (hasSampleSeq) {
        if (slot.lastAdsSampleSeqValid && sampleSeq == slot.lastAdsSampleSeq) return false;
        slot.lastAdsSampleSeq = sampleSeq;
        slot.lastAdsSampleSeqValid = true;
    }

    if (raw < slot.def.minValid || raw > slot.def.maxValid) {
        slot.endpoint->update(raw, false, nowMs);
        return false;
    }

    float filtered = slot.median.update(raw);
    float calibrated = (slot.def.c0 * filtered) + slot.def.c1;
    float rounded = ioRoundToPrecision(calibrated, slot.def.precision);

    // Trace pH/ORP/PSI calculation chain with configurable periodic ticker.
    bool isAdsSource = (slot.def.source == IO_SRC_ADS_INTERNAL_SINGLE) ||
                       (slot.def.source == IO_SRC_ADS_EXTERNAL_DIFF);
    if (cfgData_.traceEnabled && isAdsSource && idx < 3) {
        uint32_t periodMs = (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : 5000U;
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
        inputEp->update(false, false, nowMs);
        slot.lastValid = false;
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
    snprintf(out, outLen, "{{ value_json.value | float(none) | round(%ld) }}", (long)p);
}

void IOModule::registerHaAnalogSensors_()
{
    if (!haSvc_ || !haSvc_->addSensor) return;

    buildHaValueTemplate_(0, haValueTpl_[0], sizeof(haValueTpl_[0]));
    buildHaValueTemplate_(1, haValueTpl_[1], sizeof(haValueTpl_[1]));
    buildHaValueTemplate_(2, haValueTpl_[2], sizeof(haValueTpl_[2]));
    buildHaValueTemplate_(3, haValueTpl_[3], sizeof(haValueTpl_[3]));
    buildHaValueTemplate_(4, haValueTpl_[4], sizeof(haValueTpl_[4]));

    const HASensorEntry s0{"io", "orp", "ORP", "rt/io/input/a0", haValueTpl_[0], nullptr, "mdi:flash", "mV"};
    const HASensorEntry s1{"io", "ph", "pH", "rt/io/input/a1", haValueTpl_[1], nullptr, "mdi:ph", ""};
    const HASensorEntry s2{"io", "psi", "PSI", "rt/io/input/a2", haValueTpl_[2], nullptr, "mdi:gauge", "PSI"};
    const HASensorEntry s3{"io", "spare", "Spare", "rt/io/input/a3", haValueTpl_[3], nullptr, "mdi:sine-wave", nullptr};
    const HASensorEntry s4{"io", "water_temperature", "Water Temperature", "rt/io/input/a4", haValueTpl_[4], nullptr, "mdi:water-thermometer", "\xC2\xB0""C"};
    const HASensorEntry s5{"io", "air_temperature", "Air Temperature", "rt/io/input/a5", "{{ value_json.value | float(none) | round(1) }}", nullptr, "mdi:thermometer", "\xC2\xB0""C"};
    (void)haSvc_->addSensor(haSvc_->ctx, &s0);
    (void)haSvc_->addSensor(haSvc_->ctx, &s1);
    (void)haSvc_->addSensor(haSvc_->ctx, &s2);
    (void)haSvc_->addSensor(haSvc_->ctx, &s3);
    (void)haSvc_->addSensor(haSvc_->ctx, &s4);
    (void)haSvc_->addSensor(haSvc_->ctx, &s5);
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
        (void)setIoEndpointFloat(*dataStore_, analogIdx, republished, nowMs, DIRTY_SENSORS);
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
    uint8_t changedMask = 0;
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        int32_t p = clampPrecisionForHa_(analogCfg_[i].precision);
        if (haPrecisionLast_[i] == p) continue;
        haPrecisionLast_[i] = p;
        if (runtimeReady_ && i < MAX_ANALOG_ENDPOINTS && analogSlots_[i].used) {
            analogSlots_[i].def.precision = p;
        }
        changedMask |= (uint8_t)(1u << i);
        changed = true;
    }

    if (changed) {
        LOGI("Input precision changed -> publish runtime snapshot");
        const uint32_t nowMs = millis();
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            if ((changedMask & (uint8_t)(1u << i)) == 0) continue;
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
            (void)setIoEndpointBool(*dataStore_, rtIdx, in.v.b, in.timestampMs, DIRTY_ACTUATORS);
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

    if (!runtimeReady_) {
        if (!configureRuntime_()) return IO_ERR_NOT_READY;
    }

    if (pcfLastEnabled_ != cfgData_.pcfEnabled) {
        if (!cfgData_.pcfEnabled && ledMaskEp_) {
            uint8_t offLogical = 0;
            uint8_t offPhysical = pcfPhysicalFromLogical_(offLogical);
            ledMaskEp_->setMask(offPhysical, nowMs);
            pcfLogicalMask_ = offLogical;
            pcfLogicalValid_ = true;
        } else if (cfgData_.pcfEnabled) {
            if (!ledMaskEp_) {
                if (!pcf_) {
                    pcf_ = new Pcf8574Driver("pcf8574_led", &i2cBus_, cfgData_.pcfAddress);
                }
                if (pcf_->begin()) {
                    ledMaskEp_ = new Pcf8574MaskEndpoint(
                        "status_leds_mask",
                        [](void* ctx, uint8_t mask) -> bool {
                            return static_cast<Pcf8574Driver*>(ctx)->writeMask(mask);
                        },
                        [](void* ctx, uint8_t* mask) -> bool {
                            if (!mask) return false;
                            return static_cast<Pcf8574Driver*>(ctx)->readMask(*mask);
                        },
                        pcf_
                    );
                    registry_.add(ledMaskEp_);
                }
            }
            if (ledMaskEp_) {
                setLedMask_(cfgData_.pcfMaskDefault, nowMs);
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

        analogSlots_[i].endpoint = new AnalogSensorEndpoint(analogSlots_[i].def.id);
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
            s.driver = new GpioDriver(
                s.endpointId,
                s.inDef.pin,
                false,
                s.inDef.activeHigh,
                s.inDef.pullMode
            );
            if (!s.driver->begin()) continue;

            s.endpoint = new DigitalSensorEndpoint(s.endpointId);
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

        s.driver = new GpioDriver(s.outDef.id, s.outDef.pin, true, s.outDef.activeHigh);
        if (!s.driver->begin()) continue;
        s.driver->write(s.outDef.initialOn);

        if (s.outDef.momentary) {
            s.pulseTimer = xTimerCreate(
                s.outDef.id,
                pdMS_TO_TICKS((s.outDef.pulseMs == 0) ? 500u : (uint32_t)s.outDef.pulseMs),
                pdFALSE,
                &s,
                &IOModule::digitalPulseTimerCb_
            );
        }

        s.endpoint = static_cast<IOEndpoint*>(new DigitalActuatorEndpoint(
            s.outDef.id,
            &IOModule::writeDigitalOut_,
            &s
        ));
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
        adsInternal_ = new Ads1115Driver("ads_internal", &i2cBus_, adsInternalCfg);
        if (!adsInternal_->begin()) {
            LOGW("ADS internal not detected at 0x%02X", cfgData_.adsInternalAddr);
            delete adsInternal_;
            adsInternal_ = nullptr;
        }
    }

    if (needAdsExternal) {
        adsExternal_ = new Ads1115Driver("ads_external", &i2cBus_, adsExternalCfg);
        if (!adsExternal_->begin()) {
            LOGW("ADS external not detected at 0x%02X", cfgData_.adsExternalAddr);
            delete adsExternal_;
            adsExternal_ = nullptr;
        }
    }

    Ds18b20DriverConfig dsCfg{};
    dsCfg.pollMs = (cfgData_.dsPollMs < 750) ? 750 : (uint32_t)cfgData_.dsPollMs;
    dsCfg.conversionWaitMs = 750;

    if (needDsWater && oneWireWater_) {
        oneWireWater_->begin();
        if (oneWireWater_->getAddress(0, oneWireWaterAddr_)) {
            dsWater_ = new Ds18b20Driver("ds18_water", oneWireWater_, oneWireWaterAddr_, dsCfg);
            dsWater_->begin();
        } else {
            LOGW("No DS18B20 found on water OneWire bus");
        }
    }

    if (needDsAir && oneWireAir_) {
        oneWireAir_->begin();
        if (oneWireAir_->getAddress(0, oneWireAirAddr_)) {
            dsAir_ = new Ds18b20Driver("ds18_air", oneWireAir_, oneWireAirAddr_, dsCfg);
            dsAir_->begin();
        } else {
            LOGW("No DS18B20 found on air OneWire bus");
        }
    }

    if (cfgData_.pcfEnabled) {
        pcf_ = new Pcf8574Driver("pcf8574_led", &i2cBus_, cfgData_.pcfAddress);
        if (pcf_->begin()) {
            ledMaskEp_ = new Pcf8574MaskEndpoint(
                "status_leds_mask",
                [](void* ctx, uint8_t mask) -> bool {
                    return static_cast<Pcf8574Driver*>(ctx)->writeMask(mask);
                },
                [](void* ctx, uint8_t* mask) -> bool {
                    if (!mask) return false;
                    return static_cast<Pcf8574Driver*>(ctx)->readMask(*mask);
                },
                pcf_
            );
            registry_.add(ledMaskEp_);
            setLedMask_(cfgData_.pcfMaskDefault, millis());
        } else {
            LOGW("PCF8574 not detected at 0x%02X", cfgData_.pcfAddress);
            delete pcf_;
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
    if (s->pulseTimer) {
        (void)xTimerStop(s->pulseTimer, 0);
        (void)xTimerChangePeriod(s->pulseTimer, pdMS_TO_TICKS(pulse), 0);
        (void)xTimerStart(s->pulseTimer, 0);
    }
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

void IOModule::digitalPulseTimerCb_(TimerHandle_t timer)
{
    if (!timer) return;
    IOModule::DigitalSlot* s = static_cast<IOModule::DigitalSlot*>(pvTimerGetTimerID(timer));
    if (!s || !s->driver) return;
    if (!s->used || s->kind != DIGITAL_SLOT_OUTPUT) return;
    (void)s->driver->write(false);
}

void IOModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>("loghub");
    haSvc_ = services.get<HAService>("ha");
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    (void)services.add("io", &ioSvc_);

    // Default labels for digital output slots (can be overridden by persisted config).
    snprintf(digitalCfg_[0].name, sizeof(digitalCfg_[0].name), "Filtration Pump");
    snprintf(digitalCfg_[1].name, sizeof(digitalCfg_[1].name), "pH Pump");
    snprintf(digitalCfg_[2].name, sizeof(digitalCfg_[2].name), "Chlorine Pump");
    snprintf(digitalCfg_[3].name, sizeof(digitalCfg_[3].name), "Chlorine Generator");
    snprintf(digitalCfg_[4].name, sizeof(digitalCfg_[4].name), "Robot");
    snprintf(digitalCfg_[5].name, sizeof(digitalCfg_[5].name), "Lights");
    snprintf(digitalCfg_[6].name, sizeof(digitalCfg_[6].name), "Fill Pump");
    snprintf(digitalCfg_[7].name, sizeof(digitalCfg_[7].name), "Water Heater");

    cfg.registerVar(enabledVar_);
    cfg.registerVar(i2cSdaVar_);
    cfg.registerVar(i2cSclVar_);
    cfg.registerVar(adsPollVar_);
    cfg.registerVar(dsPollVar_);
    cfg.registerVar(digitalPollVar_);
    cfg.registerVar(adsInternalAddrVar_);
    cfg.registerVar(adsExternalAddrVar_);
    cfg.registerVar(adsGainVar_);
    cfg.registerVar(adsRateVar_);
    cfg.registerVar(pcfEnabledVar_);
    cfg.registerVar(pcfAddressVar_);
    cfg.registerVar(pcfMaskDefaultVar_);
    cfg.registerVar(pcfActiveLowVar_);
    cfg.registerVar(traceEnabledVar_);
    cfg.registerVar(tracePeriodVar_);

    cfg.registerVar(a0NameVar_); cfg.registerVar(a0SourceVar_); cfg.registerVar(a0ChannelVar_); cfg.registerVar(a0C0Var_);
    cfg.registerVar(a0C1Var_); cfg.registerVar(a0PrecVar_); cfg.registerVar(a0MinVar_); cfg.registerVar(a0MaxVar_);

    cfg.registerVar(a1NameVar_); cfg.registerVar(a1SourceVar_); cfg.registerVar(a1ChannelVar_); cfg.registerVar(a1C0Var_);
    cfg.registerVar(a1C1Var_); cfg.registerVar(a1PrecVar_); cfg.registerVar(a1MinVar_); cfg.registerVar(a1MaxVar_);

    cfg.registerVar(a2NameVar_); cfg.registerVar(a2SourceVar_); cfg.registerVar(a2ChannelVar_); cfg.registerVar(a2C0Var_);
    cfg.registerVar(a2C1Var_); cfg.registerVar(a2PrecVar_); cfg.registerVar(a2MinVar_); cfg.registerVar(a2MaxVar_);

    cfg.registerVar(a3NameVar_); cfg.registerVar(a3SourceVar_); cfg.registerVar(a3ChannelVar_); cfg.registerVar(a3C0Var_);
    cfg.registerVar(a3C1Var_); cfg.registerVar(a3PrecVar_); cfg.registerVar(a3MinVar_); cfg.registerVar(a3MaxVar_);

    cfg.registerVar(a4NameVar_); cfg.registerVar(a4SourceVar_); cfg.registerVar(a4ChannelVar_); cfg.registerVar(a4C0Var_);
    cfg.registerVar(a4C1Var_); cfg.registerVar(a4PrecVar_); cfg.registerVar(a4MinVar_); cfg.registerVar(a4MaxVar_);

    cfg.registerVar(d0NameVar_); cfg.registerVar(d0PinVar_); cfg.registerVar(d0ActiveHighVar_); cfg.registerVar(d0InitialOnVar_); cfg.registerVar(d0MomentaryVar_); cfg.registerVar(d0PulseVar_);
    cfg.registerVar(d1NameVar_); cfg.registerVar(d1PinVar_); cfg.registerVar(d1ActiveHighVar_); cfg.registerVar(d1InitialOnVar_); cfg.registerVar(d1MomentaryVar_); cfg.registerVar(d1PulseVar_);
    cfg.registerVar(d2NameVar_); cfg.registerVar(d2PinVar_); cfg.registerVar(d2ActiveHighVar_); cfg.registerVar(d2InitialOnVar_); cfg.registerVar(d2MomentaryVar_); cfg.registerVar(d2PulseVar_);
    cfg.registerVar(d3NameVar_); cfg.registerVar(d3PinVar_); cfg.registerVar(d3ActiveHighVar_); cfg.registerVar(d3InitialOnVar_); cfg.registerVar(d3MomentaryVar_); cfg.registerVar(d3PulseVar_);
    cfg.registerVar(d4NameVar_); cfg.registerVar(d4PinVar_); cfg.registerVar(d4ActiveHighVar_); cfg.registerVar(d4InitialOnVar_); cfg.registerVar(d4MomentaryVar_); cfg.registerVar(d4PulseVar_);
    cfg.registerVar(d5NameVar_); cfg.registerVar(d5PinVar_); cfg.registerVar(d5ActiveHighVar_); cfg.registerVar(d5InitialOnVar_); cfg.registerVar(d5MomentaryVar_); cfg.registerVar(d5PulseVar_);
    cfg.registerVar(d6NameVar_); cfg.registerVar(d6PinVar_); cfg.registerVar(d6ActiveHighVar_); cfg.registerVar(d6InitialOnVar_); cfg.registerVar(d6MomentaryVar_); cfg.registerVar(d6PulseVar_);
    cfg.registerVar(d7NameVar_); cfg.registerVar(d7PinVar_); cfg.registerVar(d7ActiveHighVar_); cfg.registerVar(d7InitialOnVar_); cfg.registerVar(d7MomentaryVar_); cfg.registerVar(d7PulseVar_);

    LOGI("I/O config registered");
    if (haSvc_ && haSvc_->addSwitch) {
        registerHaAnalogSensors_();
        const HASwitchEntry sw0{
            "io", "filtration_pump", "Filtration Pump", "rt/io/output/d0",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":0,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":0,\\\"value\\\":false}}",
            "mdi:pool"
        };
        const HASwitchEntry sw1{
            "io", "ph_pump", "pH Pump", "rt/io/output/d1",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":1,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":1,\\\"value\\\":false}}",
            "mdi:beaker-outline"
        };
        const HASwitchEntry sw2{
            "io", "chlorine_pump", "Chlorine Pump", "rt/io/output/d2",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":2,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":2,\\\"value\\\":false}}",
            "mdi:water-outline"
        };
        const HASwitchEntry sw3{
            "io", "chlorine_generator", "Chlorine Generator", "rt/io/output/d3",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":5,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":5,\\\"value\\\":false}}",
            "mdi:flash"
        };
        const HASwitchEntry sw4{
            "io", "robot", "Robot", "rt/io/output/d4",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":3,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":3,\\\"value\\\":false}}",
            "mdi:robot-vacuum"
        };
        const HASwitchEntry sw5{
            "io", "lights", "Lights", "rt/io/output/d5",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":6,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":6,\\\"value\\\":false}}",
            "mdi:lightbulb"
        };
        const HASwitchEntry sw6{
            "io", "fill_pump", "Fill Pump", "rt/io/output/d6",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":4,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":4,\\\"value\\\":false}}",
            "mdi:water-plus"
        };
        const HASwitchEntry sw7{
            "io", "water_heater", "Water Heater", "rt/io/output/d7",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":7,\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"slot\\\":7,\\\"value\\\":false}}",
            "mdi:water-boiler"
        };
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw0);
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw1);
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw2);
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw3);
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw4);
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw5);
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw6);
        (void)haSvc_->addSwitch(haSvc_->ctx, &sw7);
    }
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        haPrecisionLast_[i] = clampPrecisionForHa_(analogCfg_[i].precision);
    }
    haPrecisionLastInit_ = true;
    (void)logHub_;
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
