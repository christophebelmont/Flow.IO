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

static const char* findJsonStringValueLocal(const char* json, const char* key)
{
    static char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(json ? json : "", pat);
    if (!p) return nullptr;
    return p + strlen(pat);
}

static const char* findJsonValueLocal(const char* json, const char* key)
{
    static char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(json ? json : "", pat);
    if (!p) return nullptr;
    return p + strlen(pat);
}

static const char* skipWsLocal(const char* p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

static bool isInputEndpointIdLocal(const char* id)
{
    return id && id[0] == 'a' && id[1] >= '0' && id[1] <= '9' && id[2] == '\0';
}

static bool isOutputEndpointIdLocal(const char* id)
{
    if (!id || id[0] == '\0') return false;
    if (id[0] == 'd' && id[1] >= '0' && id[1] <= '9' && id[2] == '\0') return true;
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

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (analogSlots_[i].used) continue;
        analogSlots_[i].used = true;
        analogSlots_[i].def = def;

        if (i < ANALOG_CFG_SLOTS) {
            strncpy(analogCfg_[i].name, def.id, sizeof(analogCfg_[i].name) - 1);
            analogCfg_[i].name[sizeof(analogCfg_[i].name) - 1] = '\0';
            analogCfg_[i].source = def.source;
            analogCfg_[i].channel = def.channel;
            analogCfg_[i].c0 = def.c0;
            analogCfg_[i].c1 = def.c1;
            analogCfg_[i].precision = def.precision;
            analogCfg_[i].minValid = def.minValid;
            analogCfg_[i].maxValid = def.maxValid;
        }

        return true;
    }

    return false;
}

bool IOModule::defineDigitalOutput(const IODigitalOutputDefinition& def)
{
    if (def.id[0] == '\0') return false;
    if (def.pin == 0) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_OUTPUTS; ++i) {
        if (digitalOutSlots_[i].used) continue;
        digitalOutSlots_[i].used = true;
        digitalOutSlots_[i].def = def;

        if (i < DIGITAL_CFG_SLOTS) {
            strncpy(digitalCfg_[i].name, def.id, sizeof(digitalCfg_[i].name) - 1);
            digitalCfg_[i].name[sizeof(digitalCfg_[i].name) - 1] = '\0';
            digitalCfg_[i].pin = def.pin;
            digitalCfg_[i].activeHigh = def.activeHigh;
            digitalCfg_[i].initialOn = def.initialOn;
            digitalCfg_[i].momentary = def.momentary;
            digitalCfg_[i].pulseMs = (int32_t)def.pulseMs;
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
    if (endpointId[0] == 'a' && endpointId[1] >= '0' && endpointId[1] <= '9' && endpointId[2] == '\0') {
        uint8_t idx = (uint8_t)(endpointId[1] - '0');
        if (idx < ANALOG_CFG_SLOTS && analogCfg_[idx].name[0] != '\0') return analogCfg_[idx].name;
    }
    if (endpointId[0] == 'd' && endpointId[1] >= '0' && endpointId[1] <= '9' && endpointId[2] == '\0') {
        uint8_t idx = (uint8_t)(endpointId[1] - '0');
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
    for (uint8_t i = 0; i < MAX_DIGITAL_OUTPUTS; ++i) {
        if (digitalOutSlots_[i].used) ++count;
    }
    return count;
}

bool IOModule::runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, bool& inputGroupOut, uint8_t& slotIdxOut) const
{
    uint8_t seen = 0;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        if (seen == snapshotIdx) {
            inputGroupOut = true;
            slotIdxOut = i;
            return true;
        }
        ++seen;
    }
    for (uint8_t i = 0; i < MAX_DIGITAL_OUTPUTS; ++i) {
        if (!digitalOutSlots_[i].used) continue;
        if (seen == snapshotIdx) {
            inputGroupOut = false;
            slotIdxOut = i;
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
    bool inputGroup = false;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, inputGroup, slotIdx)) return nullptr;

    static char suffix[24];
    if (inputGroup) {
        snprintf(suffix, sizeof(suffix), "rt/io/input/a%u", (unsigned)slotIdx);
    } else {
        snprintf(suffix, sizeof(suffix), "rt/io/output/d%u", (unsigned)slotIdx);
    }
    return suffix;
}

bool IOModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    bool inputGroup = false;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, inputGroup, slotIdx)) return false;

    IOEndpoint* ep = nullptr;
    if (inputGroup) {
        ep = static_cast<IOEndpoint*>(analogSlots_[slotIdx].endpoint);
    } else {
        ep = static_cast<IOEndpoint*>(digitalOutSlots_[slotIdx].endpoint);
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
            const char* sensor = (idx == 0) ? "pH" : ((idx == 1) ? "ORP" : "PSI");
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
        if (slot.def.onValueChanged) {
            slot.def.onValueChanged(slot.def.onValueCtx, rounded);
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

    const HASensorEntry s0{"io", "ph", "pH", "rt/io/input/a0", haValueTpl_[0], nullptr, "mdi:ph", nullptr};
    const HASensorEntry s1{"io", "orp", "ORP", "rt/io/input/a1", haValueTpl_[1], nullptr, "mdi:flash", "mV"};
    const HASensorEntry s2{"io", "psi", "PSI", "rt/io/input/a2", haValueTpl_[2], nullptr, "mdi:gauge", "PSI"};
    const HASensorEntry s3{"io", "water_temperature", "Water Temperature", "rt/io/input/a3", haValueTpl_[3], nullptr, "mdi:water-thermometer", "\xC2\xB0""C"};
    const HASensorEntry s4{"io", "air_temperature", "Air Temperature", "rt/io/input/a4", haValueTpl_[4], nullptr, "mdi:thermometer", "\xC2\xB0""C"};
    (void)haSvc_->addSensor(haSvc_->ctx, &s0);
    (void)haSvc_->addSensor(haSvc_->ctx, &s1);
    (void)haSvc_->addSensor(haSvc_->ctx, &s2);
    (void)haSvc_->addSensor(haSvc_->ctx, &s3);
    (void)haSvc_->addSensor(haSvc_->ctx, &s4);
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

    for (uint8_t i = 0; i < MAX_DIGITAL_OUTPUTS; ++i) {
        if (!digitalOutSlots_[i].used) continue;

        DigitalOutputSlot& s = digitalOutSlots_[i];
        if (i < DIGITAL_CFG_SLOTS) {
            snprintf(s.def.id, sizeof(s.def.id), "d%u", (unsigned)i);
            if (digitalCfg_[i].pin != 0) s.def.pin = digitalCfg_[i].pin;
            s.def.activeHigh = digitalCfg_[i].activeHigh;
            s.def.initialOn = digitalCfg_[i].initialOn;
            s.def.momentary = digitalCfg_[i].momentary;
            int32_t p = digitalCfg_[i].pulseMs;
            if (p <= 0) p = 500;
            if (p > 60000) p = 60000;
            s.def.pulseMs = (uint16_t)p;
        }

        s.driver = new GpioDriver(s.def.id, s.def.pin, true, s.def.activeHigh);
        if (!s.driver->begin()) continue;
        s.driver->write(s.def.initialOn);

        if (s.def.momentary) {
            s.pulseTimer = xTimerCreate(
                s.def.id,
                pdMS_TO_TICKS((s.def.pulseMs == 0) ? 500u : (uint32_t)s.def.pulseMs),
                pdFALSE,
                &s,
                &IOModule::digitalPulseTimerCb_
            );
        }

        s.endpoint = new DigitalActuatorEndpoint(
            s.def.id,
            &IOModule::writeDigitalOut_,
            &s
        );
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
            if (services_) services_->add("io_leds", &ledSvc_);
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

    runtimeReady_ = true;
    pcfLastEnabled_ = cfgData_.pcfEnabled;

    LOGI("I/O ready (ads=%ldms ds=%ldms endpoints=%u pcf=%s)",
         (long)adsJob.periodMs,
         (long)dsJob.periodMs,
         (unsigned)registry_.count(),
         cfgData_.pcfEnabled ? "on" : "off");

    return true;
}

bool IOModule::writeDigitalOut_(void* ctx, bool on)
{
    IOModule::DigitalOutputSlot* s = static_cast<IOModule::DigitalOutputSlot*>(ctx);
    if (!s || !s->driver) return false;

    if (!s->def.momentary) {
        return s->driver->write(on);
    }

    // Momentary outputs always generate a physical pulse on each command.
    if (!s->driver->write(true)) return false;
    uint32_t pulse = (s->def.pulseMs == 0) ? 500u : (uint32_t)s->def.pulseMs;
    if (s->pulseTimer) {
        (void)xTimerStop(s->pulseTimer, 0);
        (void)xTimerChangePeriod(s->pulseTimer, pdMS_TO_TICKS(pulse), 0);
        (void)xTimerStart(s->pulseTimer, 0);
    }
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
    IOModule::DigitalOutputSlot* s = static_cast<IOModule::DigitalOutputSlot*>(pvTimerGetTimerID(timer));
    if (!s || !s->driver) return;
    (void)s->driver->write(false);
}

bool IOModule::cmdIoWrite_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    IOModule* self = static_cast<IOModule*>(userCtx);
    if (!self) return false;
    return self->handleIoWrite_(req, reply, replyLen);
}

bool IOModule::handleIoWrite_(const CommandRequest& req, char* reply, size_t replyLen)
{
    const char* json = req.args ? req.args : req.json;
    if (!json) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_args\"}");
        return false;
    }

    const char* idStart = findJsonStringValueLocal(json, "id");
    idStart = skipWsLocal(idStart);
    if (!idStart) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_id\"}");
        return false;
    }
    const char* idEnd = strchr(idStart, '"');
    if (!idEnd) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"bad_id\"}");
        return false;
    }

    char id[32] = {0};
    size_t idLen = (size_t)(idEnd - idStart);
    if (idLen >= sizeof(id)) idLen = sizeof(id) - 1;
    memcpy(id, idStart, idLen);
    id[idLen] = '\0';
    IOEndpoint* ep = registry_.find(id);
    if (!ep) {
        LOGW("io.write unknown endpoint id=%s", id);
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"unknown_endpoint\"}");
        return false;
    }
    if ((ep->capabilities() & IO_CAP_WRITE) == 0) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"readonly_endpoint\"}");
        return false;
    }

    const char* valuePos = findJsonValueLocal(json, "value");
    valuePos = skipWsLocal(valuePos);
    if (!valuePos) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_value\"}");
        return false;
    }

    if (strcmp(id, "status_leds_mask") == 0) {
        int32_t logicalMask = (int32_t)strtol(valuePos, nullptr, 10);
        bool ok = setLedMask_((uint8_t)(logicalMask & 0xFF), millis());
        snprintf(reply, replyLen, "{\"ok\":%s,\"id\":\"%s\"}", ok ? "true" : "false", id);
        return ok;
    }

    IOEndpointValue cur{};
    ep->read(cur);

    IOEndpointValue in{};
    in.timestampMs = millis();
    if (cur.valueType == IO_EP_VALUE_BOOL) {
        bool requested = false;
        bool hasRequested = true;
        if (strncmp(valuePos, "true", 4) == 0) {
            requested = true;
        } else if (strncmp(valuePos, "false", 5) == 0) {
            requested = false;
        } else {
            int v = atoi(valuePos);
            requested = (v != 0);
        }

        // For momentary outputs, io.write is a "set virtual state" command:
        // pulse only when requested state differs from current virtual state.
        bool isMomentary = false;
        for (uint8_t i = 0; i < MAX_DIGITAL_OUTPUTS; ++i) {
            DigitalOutputSlot& s = digitalOutSlots_[i];
            if (!s.used) continue;
            if (!s.def.momentary) continue;
            if (strcmp(s.def.id, id) != 0) continue;
            isMomentary = true;
            break;
        }

        if (isMomentary) {
            if (hasRequested && cur.valid && cur.valueType == IO_EP_VALUE_BOOL && cur.v.b == requested) {
                // No-op: requested virtual state already applied.
                snprintf(reply, replyLen, "{\"ok\":true,\"id\":\"%s\",\"noop\":true}", id);
                return true;
            }
            in.valueType = IO_EP_VALUE_BOOL;
            in.v.b = requested;
        } else {
            in.valueType = IO_EP_VALUE_BOOL;
            in.v.b = requested;
        }
    } else if (cur.valueType == IO_EP_VALUE_INT32) {
        in.valueType = IO_EP_VALUE_INT32;
        in.v.i = (int32_t)strtol(valuePos, nullptr, 10);
    } else {
        in.valueType = IO_EP_VALUE_FLOAT;
        in.v.f = (float)atof(valuePos);
    }
    in.valid = true;

    bool ok = ep->write(in);
    if (ok && dataStore_) {
        uint8_t rtIdx = 0;
        if (endpointIndexFromId_(id, rtIdx)) {
            if (in.valueType == IO_EP_VALUE_BOOL) {
                (void)setIoEndpointBool(*dataStore_, rtIdx, in.v.b, in.timestampMs, DIRTY_ACTUATORS);
            } else if (in.valueType == IO_EP_VALUE_INT32) {
                (void)setIoEndpointInt(*dataStore_, rtIdx, in.v.i, in.timestampMs, DIRTY_ACTUATORS);
            } else if (in.valueType == IO_EP_VALUE_FLOAT) {
                (void)setIoEndpointFloat(*dataStore_, rtIdx, in.v.f, in.timestampMs, DIRTY_ACTUATORS);
            }
        }
    }
    snprintf(reply, replyLen, "{\"ok\":%s,\"id\":\"%s\"}", ok ? "true" : "false", id);
    return ok;
}

void IOModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    services_ = &services;
    logHub_ = services.get<LogHubService>("loghub");
    cmdSvc_ = services.get<CommandService>("cmd");
    haSvc_ = services.get<HAService>("ha");
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

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
    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "io.write", cmdIoWrite_, this);
        LOGI("Command registered: io.write");
    }
    if (haSvc_ && haSvc_->addSwitch) {
        registerHaAnalogSensors_();
        const HASwitchEntry sw0{
            "io", "filtration_pump", "Filtration Pump", "rt/io/output/d0",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd0\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd0\\\",\\\"value\\\":false}}",
            "mdi:pool"
        };
        const HASwitchEntry sw1{
            "io", "ph_pump", "pH Pump", "rt/io/output/d1",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd1\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd1\\\",\\\"value\\\":false}}",
            "mdi:beaker-outline"
        };
        const HASwitchEntry sw2{
            "io", "chlorine_pump", "Chlorine Pump", "rt/io/output/d2",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd2\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd2\\\",\\\"value\\\":false}}",
            "mdi:water-outline"
        };
        const HASwitchEntry sw3{
            "io", "chlorine_generator", "Chlorine Generator", "rt/io/output/d3",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d3\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d3\\\",\\\"value\\\":false}}",
            "mdi:flash"
        };
        const HASwitchEntry sw4{
            "io", "robot", "Robot", "rt/io/output/d4",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d4\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d4\\\",\\\"value\\\":false}}",
            "mdi:robot-vacuum"
        };
        const HASwitchEntry sw5{
            "io", "lights", "Lights", "rt/io/output/d5",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d5\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d5\\\",\\\"value\\\":false}}",
            "mdi:lightbulb"
        };
        const HASwitchEntry sw6{
            "io", "fill_pump", "Fill Pump", "rt/io/output/d6",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d6\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d6\\\",\\\"value\\\":false}}",
            "mdi:water-plus"
        };
        const HASwitchEntry sw7{
            "io", "water_heater", "Water Heater", "rt/io/output/d7",
            "{% if value_json.value %}ON{% else %}OFF{% endif %}", "cmd",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d7\\\",\\\"value\\\":true}}",
            "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d7\\\",\\\"value\\\":false}}",
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
    maybeRefreshHaOnPrecisionChange_();

    if (!cfgData_.enabled) {
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    if (!runtimeReady_) {
        if (!configureRuntime_()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
    }

    if (pcfLastEnabled_ != cfgData_.pcfEnabled) {
        if (!cfgData_.pcfEnabled && ledMaskEp_) {
            uint8_t offLogical = 0;
            uint8_t offPhysical = pcfPhysicalFromLogical_(offLogical);
            ledMaskEp_->setMask(offPhysical, millis());
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
                    if (services_) services_->add("io_leds", &ledSvc_);
                }
            }
            if (ledMaskEp_) {
                // Re-apply configured default on re-enable.
                setLedMask_(cfgData_.pcfMaskDefault, millis());
            }
        }
        pcfLastEnabled_ = cfgData_.pcfEnabled;
    }

    scheduler_.tick(millis());

    vTaskDelay(pdMS_TO_TICKS(10));
}
