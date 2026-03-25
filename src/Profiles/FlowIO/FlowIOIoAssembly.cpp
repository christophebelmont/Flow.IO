#include "Profiles/FlowIO/FlowIOIoAssembly.h"

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "App/AppContext.h"
#include "Board/BoardSpec.h"
#include "Board/BoardSerialMap.h"
#include "Core/MqttTopics.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/IOModule/IORuntime.h"
#include "Profiles/FlowIO/FlowIOProfile.h"

namespace {

using Profiles::FlowIO::ModuleInstances;
static constexpr uint32_t kWaterCounterDebounceUs = 100000U;  // Accept at most 1 pulse every 100 ms.

struct FlowIoAnalogHaSpec {
    uint8_t analogIdx = 0;
    const char* objectSuffix = nullptr;
    const char* name = nullptr;
    const char* icon = nullptr;
    const char* unit = nullptr;
};

struct FlowIoDigitalHaSpec {
    uint8_t logicalIdx = 0;
    const char* objectSuffix = nullptr;
    const char* name = nullptr;
    const char* icon = nullptr;
};

constexpr FlowIoAnalogHaSpec kAnalogHaSpecs[] = {
    {0, "io_orp", "ORP", "mdi:flash", "mV"},
    {1, "io_ph", "pH", "mdi:ph", ""},
    {2, "io_psi", "PSI", "mdi:gauge", "PSI"},
    {3, "io_spare", "Spare", "mdi:sine-wave", nullptr},
    {4, "io_wat_tmp", "Water Temperature", "mdi:water-thermometer", "\xC2\xB0""C"},
    {5, "io_air_tmp", "Air Temperature", "mdi:thermometer", "\xC2\xB0""C"},
};

constexpr FlowIoDigitalHaSpec kDigitalHaSpecs[] = {
    {0, "io_pool_lvl", "Pool Level", "mdi:waves-arrow-up"},
    {1, "io_ph_lvl", "pH Level", "mdi:flask-outline"},
    {2, "io_chl_lvl", "Chlorine Level", "mdi:test-tube"},
    {3, "io_wat_cnt", "Water Counter", "mdi:water-sync"},
};

struct FlowIoDiscoveryHeap {
    char analogValueTpl[sizeof(kAnalogHaSpecs) / sizeof(kAnalogHaSpecs[0])][128]{};
    char switchPayloadOn[PoolBinding::kDeviceBindingCount][Limits::IoHaSwitchPayloadBuf]{};
    char switchPayloadOff[PoolBinding::kDeviceBindingCount][Limits::IoHaSwitchPayloadBuf]{};
};

// Small discovery state suffixes stay static; large templates/payloads move to boot heap.
char gAnalogStateSuffix[sizeof(kAnalogHaSpecs) / sizeof(kAnalogHaSpecs[0])][24]{};
char gDigitalStateSuffix[sizeof(kDigitalHaSpecs) / sizeof(kDigitalHaSpecs[0])][24]{};
char gSwitchStateSuffix[PoolBinding::kDeviceBindingCount][24]{};
FlowIoDiscoveryHeap* gDiscoveryHeap = nullptr;

bool ensureDiscoveryHeap()
{
    if (gDiscoveryHeap) return true;
    gDiscoveryHeap = static_cast<FlowIoDiscoveryHeap*>(
        heap_caps_calloc(1, sizeof(FlowIoDiscoveryHeap), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    return gDiscoveryHeap != nullptr;
}

const DomainIoBinding* findBindingByRole(const DomainSpec& domain, DomainRole role)
{
    for (uint8_t i = 0; i < domain.ioBindingCount; ++i) {
        const DomainIoBinding& binding = domain.ioBindings[i];
        if (binding.role == role) return &binding;
    }
    return nullptr;
}

const DomainIoBinding* findBindingBySignal(const DomainSpec& domain, BoardSignal signal)
{
    for (uint8_t i = 0; i < domain.ioBindingCount; ++i) {
        const DomainIoBinding& binding = domain.ioBindings[i];
        if (binding.signal == signal) return &binding;
    }
    return nullptr;
}

const PoolDevicePreset* findPoolPresetByRole(const DomainSpec& domain, DomainRole role)
{
    for (uint8_t i = 0; i < domain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = domain.poolDevices[i];
        if (preset.role == role) return &preset;
    }
    return nullptr;
}

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    while (true) delay(1000);
}

void onIoFloatValue(void* ctx, float value)
{
    ModuleInstances& modules = Profiles::FlowIO::moduleInstances();
    if (!modules.ioDataStore) return;
    const uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointFloat(*modules.ioDataStore, idx, value, millis());
}

void onIoBoolValue(void* ctx, bool value)
{
    ModuleInstances& modules = Profiles::FlowIO::moduleInstances();
    if (!modules.ioDataStore) return;
    const uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointBool(*modules.ioDataStore, idx, value, millis());
}

void onIoIntValue(void* ctx, int32_t value)
{
    ModuleInstances& modules = Profiles::FlowIO::moduleInstances();
    if (!modules.ioDataStore) return;
    const uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointInt(*modules.ioDataStore, idx, value, millis());
}

void applyAnalogDefaultsForRole(DomainRole role, IOAnalogDefinition& def)
{
    switch (role) {
        case DomainRole::OrpSensor:
            def.source = FLOW_WIRDEF_IO_A0S;
            def.channel = FLOW_WIRDEF_IO_A0C;
            def.c0 = FLOW_WIRDEF_IO_A00;
            def.c1 = FLOW_WIRDEF_IO_A01;
            def.precision = FLOW_WIRDEF_IO_A0P;
            def.minValid = FLOW_WIRDEF_IO_A0N;
            def.maxValid = FLOW_WIRDEF_IO_A0X;
            break;
        case DomainRole::PhSensor:
            def.source = FLOW_WIRDEF_IO_A1S;
            def.channel = FLOW_WIRDEF_IO_A1C;
            def.c0 = FLOW_WIRDEF_IO_A10;
            def.c1 = FLOW_WIRDEF_IO_A11;
            def.precision = FLOW_WIRDEF_IO_A1P;
            def.minValid = FLOW_WIRDEF_IO_A1N;
            def.maxValid = FLOW_WIRDEF_IO_A1X;
            break;
        case DomainRole::PsiSensor:
            def.source = FLOW_WIRDEF_IO_A2S;
            def.channel = FLOW_WIRDEF_IO_A2C;
            def.c0 = FLOW_WIRDEF_IO_A20;
            def.c1 = FLOW_WIRDEF_IO_A21;
            def.precision = FLOW_WIRDEF_IO_A2P;
            def.minValid = FLOW_WIRDEF_IO_A2N;
            def.maxValid = FLOW_WIRDEF_IO_A2X;
            break;
        case DomainRole::SpareAnalog:
            def.source = FLOW_WIRDEF_IO_A3S;
            def.channel = FLOW_WIRDEF_IO_A3C;
            def.c0 = FLOW_WIRDEF_IO_A30;
            def.c1 = FLOW_WIRDEF_IO_A31;
            def.precision = FLOW_WIRDEF_IO_A3P;
            def.minValid = FLOW_WIRDEF_IO_A3N;
            def.maxValid = FLOW_WIRDEF_IO_A3X;
            break;
        case DomainRole::WaterTemp:
            def.source = FLOW_WIRDEF_IO_A4S;
            def.channel = FLOW_WIRDEF_IO_A4C;
            def.c0 = FLOW_WIRDEF_IO_A40;
            def.c1 = FLOW_WIRDEF_IO_A41;
            def.precision = FLOW_WIRDEF_IO_A4P;
            def.minValid = FLOW_WIRDEF_IO_A4N;
            def.maxValid = FLOW_WIRDEF_IO_A4X;
            break;
        case DomainRole::AirTemp:
            def.source = FLOW_WIRDEF_IO_A5S;
            def.channel = FLOW_WIRDEF_IO_A5C;
            def.c0 = FLOW_WIRDEF_IO_A50;
            def.c1 = FLOW_WIRDEF_IO_A51;
            def.precision = FLOW_WIRDEF_IO_A5P;
            def.minValid = FLOW_WIRDEF_IO_A5N;
            def.maxValid = FLOW_WIRDEF_IO_A5X;
            break;
        default:
            requireSetup(false, "unsupported analog domain role");
            break;
    }
}

void applyDigitalDefaultsForRole(DomainRole role, IODigitalInputDefinition& def)
{
    switch (role) {
        case DomainRole::WaterCounterSensor:
            def.mode = IO_DIGITAL_INPUT_COUNTER;
            def.counterDebounceUs = kWaterCounterDebounceUs;
            break;
        default:
            break;
    }
}

void buildAnalogValueTemplate(const IOModule& ioModule, uint8_t analogIdx, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    const int32_t precision = ioModule.analogPrecision(analogIdx);
    snprintf(
        out,
        outLen,
        "{%% if value_json.value is number %%}{{ value_json.value | float | round(%ld) }}{%% else %%}unavailable{%% endif %%}",
        (long)precision
    );
}

void syncAnalogSensors(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addSensor) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";

    for (uint8_t i = 0; i < (uint8_t)(sizeof(kAnalogHaSpecs) / sizeof(kAnalogHaSpecs[0])); ++i) {
        const FlowIoAnalogHaSpec& spec = kAnalogHaSpecs[i];
        if (!modules.ioModule.analogSlotUsed(spec.analogIdx)) continue;

        buildAnalogValueTemplate(
            modules.ioModule,
            spec.analogIdx,
            gDiscoveryHeap->analogValueTpl[i],
            sizeof(gDiscoveryHeap->analogValueTpl[i])
        );
        snprintf(gAnalogStateSuffix[i], sizeof(gAnalogStateSuffix[i]), "rt/io/input/a%u", (unsigned)spec.analogIdx);
        const HASensorEntry entry{
            "io",
            spec.objectSuffix,
            spec.name,
            gAnalogStateSuffix[i],
            gDiscoveryHeap->analogValueTpl[i],
            nullptr,
            spec.icon,
            spec.unit,
            false,
            kAvailabilityTpl
        };
        (void)modules.haService->addSensor(modules.haService->ctx, &entry);
    }
}

void syncDigitalInputBinarySensors(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addBinarySensor || !modules.haService->addSensor) return;
    static constexpr const char* kBoolTpl = "{{ 'True' if value_json.value else 'False' }}";
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";
    static constexpr const char* kCountTpl =
        "{% if value_json.value is number %}{{ value_json.value | int }}{% else %}unavailable{% endif %}";

    for (uint8_t i = 0; i < (uint8_t)(sizeof(kDigitalHaSpecs) / sizeof(kDigitalHaSpecs[0])); ++i) {
        const FlowIoDigitalHaSpec& spec = kDigitalHaSpecs[i];
        if (!modules.ioModule.digitalInputSlotUsed(spec.logicalIdx)) continue;

        snprintf(gDigitalStateSuffix[i], sizeof(gDigitalStateSuffix[i]), "rt/io/input/i%u", (unsigned)spec.logicalIdx);
        if (modules.ioModule.digitalInputValueType(spec.logicalIdx) == IO_VAL_INT32) {
            const HASensorEntry entry{
                "io",
                spec.objectSuffix,
                spec.name,
                gDigitalStateSuffix[i],
                kCountTpl,
                nullptr,
                spec.icon,
                "pulses",
                false,
                kAvailabilityTpl
            };
            (void)modules.haService->addSensor(modules.haService->ctx, &entry);
            continue;
        }

        const HABinarySensorEntry entry{
            "io",
            spec.objectSuffix,
            spec.name,
            gDigitalStateSuffix[i],
            kBoolTpl,
            nullptr,
            nullptr,
            spec.icon
        };
        (void)modules.haService->addBinarySensor(modules.haService->ctx, &entry);
    }
}

void syncSwitches(ModuleInstances& modules)
{
    if (!modules.haService || !modules.haService->addSwitch) return;
    requireSetup(ensureDiscoveryHeap(), "ha discovery heap");

    for (uint8_t i = 0; i < PoolBinding::kDeviceBindingCount; ++i) {
        const PoolIoBinding& binding = PoolBinding::kIoBindings[i];
        if (binding.ioId < IO_ID_DO_BASE) continue;

        const uint8_t logical = (uint8_t)(binding.ioId - IO_ID_DO_BASE);
        if (!modules.ioModule.digitalOutputSlotUsed(logical)) continue;

        snprintf(gSwitchStateSuffix[i], sizeof(gSwitchStateSuffix[i]), "rt/io/output/d%u", (unsigned)logical);
        bool payloadOk = true;

        if (binding.slot == PoolBinding::kDeviceSlotFiltrationPump) {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":true}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":false}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        } else {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":true}}",
                (unsigned)binding.slot
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":false}}",
                (unsigned)binding.slot
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        }

        if (!payloadOk) {
            requireSetup(false, "ha switch payload");
            continue;
        }

        const HASwitchEntry entry{
            "io",
            binding.objectSuffix,
            binding.name,
            gSwitchStateSuffix[i],
            "{% if value_json.value %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCmd,
            gDiscoveryHeap->switchPayloadOn[i],
            gDiscoveryHeap->switchPayloadOff[i],
            binding.haIcon,
            nullptr
        };
        (void)modules.haService->addSwitch(modules.haService->ctx, &entry);
    }
}

}  // namespace

namespace Profiles {
namespace FlowIO {

void configureIoModule(const AppContext& ctx, ModuleInstances& modules)
{
    requireSetup(ctx.board != nullptr, "missing board spec");
    requireSetup(ctx.domain != nullptr, "missing domain spec");

    modules.ioModule.setOneWireBuses(&modules.oneWireWater, &modules.oneWireAir);

    for (uint8_t i = 0; i < ctx.domain->sensorCount; ++i) {
        const DomainSensorPreset& preset = ctx.domain->sensors[i];
        const DomainIoBinding* binding = findBindingByRole(*ctx.domain, preset.role);
        requireSetup(binding != nullptr, "missing domain sensor binding");

        const IoPointSpec* ioPoint = boardFindIoPoint(*ctx.board, binding->signal);
        requireSetup(ioPoint != nullptr, "missing board sensor point");

        const PoolSensorBinding* compat = PoolBinding::sensorBindingBySlot(preset.legacySlot);
        requireSetup(compat != nullptr, "missing compatibility sensor binding");

        if (preset.digitalInput) {
            IODigitalInputDefinition def{};
            snprintf(def.id, sizeof(def.id), "%s", compat->endpointId);
            def.ioId = compat->ioId;
            def.pin = ioPoint->pin;
            def.activeHigh = preset.activeHigh;
            def.pullMode = preset.pullMode;
            applyDigitalDefaultsForRole(preset.role, def);
            def.onValueChanged = onIoBoolValue;
            def.onValueCtx = (void*)(uintptr_t)compat->runtimeIndex;
            def.onCounterChanged = onIoIntValue;
            def.onCounterCtx = (void*)(uintptr_t)compat->runtimeIndex;
            requireSetup(modules.ioModule.defineDigitalInput(def), "define digital input");
            continue;
        }

        IOAnalogDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", compat->endpointId);
        def.ioId = compat->ioId;
        def.onValueChanged = onIoFloatValue;
        def.onValueCtx = (void*)(uintptr_t)compat->runtimeIndex;
        applyAnalogDefaultsForRole(preset.role, def);
        requireSetup(modules.ioModule.defineAnalogInput(def), "define analog input");
    }

    for (uint8_t i = 0; i < ctx.board->ioPointCount; ++i) {
        const IoPointSpec& point = ctx.board->ioPoints[i];
        if (point.capability != IoCapability::DigitalOut) continue;

        const DomainIoBinding* binding = findBindingBySignal(*ctx.domain, point.signal);
        requireSetup(binding != nullptr, "missing output domain binding");

        const PoolDevicePreset* preset = findPoolPresetByRole(*ctx.domain, binding->role);
        requireSetup(preset != nullptr, "missing output device preset");

        const PoolIoBinding* compat = PoolBinding::ioBindingBySlot(preset->legacySlot);
        requireSetup(compat != nullptr, "missing compatibility output binding");

        IODigitalOutputDefinition def{};
        snprintf(def.id, sizeof(def.id), "%s", compat->objectSuffix ? compat->objectSuffix : "output");
        def.ioId = compat->ioId;
        def.pin = point.pin;
        def.activeHigh = false;
        def.initialOn = false;
        def.momentary = point.momentary;
        def.pulseMs = point.momentary ? point.pulseMs : 0;
        requireSetup(modules.ioModule.defineDigitalOutput(def), "define digital output");
    }
}

void registerIoHomeAssistant(AppContext& ctx, ModuleInstances& modules)
{
    modules.haService = ctx.services.get<HAService>(ServiceId::Ha);
    if (!modules.haService) return;

    syncAnalogSensors(modules);
    syncDigitalInputBinarySensors(modules);
    syncSwitches(modules);

    if (modules.haService->requestRefresh) {
        (void)modules.haService->requestRefresh(modules.haService->ctx);
    }
}

void refreshIoHomeAssistantIfNeeded(ModuleInstances& modules)
{
    if (!modules.haService) return;
    const uint16_t dirtyMask = modules.ioModule.takeAnalogConfigDirtyMask();
    if (dirtyMask == 0) return;

    syncAnalogSensors(modules);
    if (modules.haService->requestRefresh) {
        (void)modules.haService->requestRefresh(modules.haService->ctx);
    }
}

}  // namespace FlowIO
}  // namespace Profiles
