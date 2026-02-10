# ModuleDevGuide

Practical guide to add a new module to Flow.IO (ESP32 + FreeRTOS), including conventions and required documentation.

## 1) Choose the Module Type

- `Module` (active): has runtime logic in `loop()` and runs in its own FreeRTOS task.
- `ModulePassive` (passive): wiring-only module (services/config), no task.

## 2) Create Module Files

At minimum:

- `src/Modules/<Name>/<Name>Module.h`
- `src/Modules/<Name>/<Name>Module.cpp`
- Optional runtime model: `src/Modules/<Name>/<Name>ModuleDataModel.h`
- Optional runtime helpers: `src/Modules/<Name>/<Name>Runtime.h`

## 3) Define the Module Contract

In the module header:

- define a stable `moduleId()`
- define `taskName()` for active modules
- declare `dependencyCount()` and `dependency(i)`
- implement `init(ConfigStore&, ServiceRegistry&)`
- implement `loop()` for active modules

## 4) ConfigStore Integration

- declare `ConfigVariable<...>` fields in the module
- register them in `init()` with `cfg.registerVar(...)`
- keep coherent `moduleName` groups (for example `mqtt`, `io/input/a0`, `pdm/pd0`)
- do not bypass `ConfigStore` for NVS writes

## 5) DataStore Integration

If your module publishes shared runtime state:

1. Add a struct in `<Name>ModuleDataModel.h`.
2. Add the `MODULE_DATA_MODEL` marker.
3. Add typed set/get helpers in `<Name>Runtime.h`.
4. Notify updates via `ds.notifyChanged(dataKey, dirtyMask)`.
5. Regenerate aggregated runtime headers:

```bash
python3 scripts/generate_datamodel.py
```

## 6) Service Design

- expose a service only for reusable callable capabilities
- register in the producer with `services.add("id", &service)`
- consume with `services.get<T>("id")` and always check for `nullptr`
- see also `docs/CoreServicesGuidelines.md`

## 7) EventBus Design

- publish events for asynchronous transitions
- keep payloads small and trivially copyable
- avoid heavy logic inside EventBus callbacks

## 8) Wiring in `main.cpp`

- instantiate the module
- add it to `ModuleManager`
- wire runtime definitions for domain modules (for example I/O and pool devices)
- keep initialization intent clear; actual ordering is enforced by dependency resolution

## 9) Mandatory Module Documentation

For each new module, add `docs/modules/<Name>Module.md` with exactly this structure:

1. `General description`
2. `Module dependencies`
3. `Provided services`
4. `Consumed services`
5. `ConfigStore values used`
6. `DataStore values used`

Use the same structure for every module to keep reviews and maintenance consistent.

## 10) Pre-Merge Checklist

- module compiles
- dependencies are correct
- services are registered/consumed correctly
- ConfigStore keys are documented
- DataStore fields/keys are documented
- `docs/modules/` entry exists for the module
- README contains the documentation link
