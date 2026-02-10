# Developer Guide - Modules, DataStore, Config, MQTT

This document shows how to build a complete module: services, configuration, runtime DataStore integration, and MQTT publication (via `MQTTModule`).

## 1) Create a Module Skeleton

Create a folder and files:

- `src/Modules/Example/ExampleModule.h`
- `src/Modules/Example/ExampleModule.cpp`
- `src/Modules/Example/ExampleModuleDataModel.h`
- `src/Modules/Example/ExampleRuntime.h`

Then add the module instance in `src/main.cpp`.

## 2) Runtime Model (DataStore)

### 2.1 Declare runtime struct

```cpp
// src/Modules/Example/ExampleModuleDataModel.h
#pragma once

struct ExampleRuntimeData {
    float value = 0.0f;
};

// MODULE_DATA_MODEL: ExampleRuntimeData example
```

### 2.2 Expose runtime helpers

```cpp
// src/Modules/Example/ExampleRuntime.h
#pragma once
#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"

// RUNTIME_PUBLIC

constexpr DataKey DATAKEY_EXAMPLE_VALUE = 42;

static inline float exampleValue(const DataStore& ds)
{
    return ds.data().example.value;
}

static inline void setExampleValue(DataStore& ds, float v)
{
    RuntimeData& rt = ds.dataMutable();
    if (rt.example.value == v) return;
    rt.example.value = v;
    ds.notifyChanged(DATAKEY_EXAMPLE_VALUE, DIRTY_SENSORS);
}
```

### 2.3 Regenerate aggregated headers

```bash
python3 scripts/generate_datamodel.py
```

## 3) Full Module (Config + Services + DataStore)

```cpp
// src/Modules/Example/ExampleModule.h
#pragma once
#include "Core/Module.h"
#include "Core/Services/Services.h"

struct ExampleConfig {
    bool enabled = true;
    int32_t periodMs = 1000;
};

class ExampleModule : public Module {
public:
    const char* moduleId() const override { return "example"; }
    const char* taskName() const override { return "example"; }

    uint8_t dependencyCount() const override { return 2; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "datastore";
        return nullptr;
    }

    void init(ConfigStore& cfg, I2CManager& i2c, ServiceRegistry& services) override;
    void loop() override;

private:
    ExampleConfig cfgData;
    const LogHubService* logHub = nullptr;
    DataStore* dataStore = nullptr;

    ConfigVariable<bool,0> enabledVar {
        NVS_KEY("ex_en"),"enabled","example",ConfigType::Bool,
        &cfgData.enabled,ConfigPersistence::Persistent,0
    };
    ConfigVariable<int32_t,0> periodVar {
        NVS_KEY("ex_per"),"period_ms","example",ConfigType::Int32,
        &cfgData.periodMs,ConfigPersistence::Persistent,0
    };
};
```

```cpp
// src/Modules/Example/ExampleModule.cpp
#include "ExampleModule.h"
#include "Modules/Example/ExampleRuntime.h"
#define LOG_TAG "Example"
#include "Core/ModuleLog.h"

void ExampleModule::init(ConfigStore& cfg, I2CManager&, ServiceRegistry& services)
{
    cfg.registerVar(enabledVar);
    cfg.registerVar(periodVar);

    logHub = services.get<LogHubService>("loghub");
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore = dsSvc ? dsSvc->store : nullptr;

    LOGI("ExampleModule init");
}

void ExampleModule::loop()
{
    if (!cfgData.enabled) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }

    if (dataStore) {
        setExampleValue(*dataStore, 42.0f);
    }

    vTaskDelay(pdMS_TO_TICKS(cfgData.periodMs));
}
```

## 4) MQTT Runtime Publication

`MQTTModule` publishes runtime snapshots through `DataSnapshotAvailable`.
Whenever a DataStore setter changes a value and sets a dirty flag, a snapshot can be published.

Typical examples:

- `rt/io/input/state` on change
- `rt/network/state` every 60s
- `rt/system/state` every 60s

If you add a new runtime block, either add a topic in `MQTTModule` or create a dedicated publisher.

## 5) Quick Checklist

- add `*ModuleDataModel.h` + `MODULE_DATA_MODEL`
- add `*Runtime.h` + `RUNTIME_PUBLIC`
- run `scripts/generate_datamodel.py`
- register `ConfigVariable` entries in `init()`
- use DataStore setters to trigger runtime notifications
