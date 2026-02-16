# Flow.IO - Pool Monitoring and Control on ESP32

Flow.IO is an ESP32 firmware designed to operate a pool with reliable, production-oriented control: continuous sensor acquisition (pH, ORP, pressure, temperatures), equipment control (pumps, chlorinator, lighting, heater, filling), runtime supervision, and metrics publishing to MQTT/Home Assistant. The product goal is straightforward: turn a pool installation into a connected, controllable, and observable real-time system with a robust, extensible architecture.

## Fully Modular ESP32 Architecture

The program is fully modular:

- each capability is implemented as an isolated module (`Module` or `ModulePassive`)
- module dependencies are explicit (`dependencyCount`, `dependency`)
- inter-module contracts are typed services registered in `ServiceRegistry`
- shared runtime state is centralized in `DataStore` (with dirty flags and events)
- persistent configuration is centralized in `ConfigStore` (NVS + JSON import/export)
- active modules run in dedicated FreeRTOS tasks

This model fits ESP32 constraints well: low coupling, controlled evolution, and incremental integration of new sensors and actuators.

## Global Project Structure

```text
src/
  Core/
    Module*.h/.cpp                 # module manager + services + runtime foundations
    Services/                      # service interfaces (IWifi, IMqtt, ITime, ...)
    DataStore/                     # runtime state + EventBus notifications
    EventBus/                      # inter-module event bus
  Modules/
    Logs/                          # log hub + dispatcher + serial sink
    Stores/                        # ConfigStoreModule + DataStoreModule
    System/                        # system commands + monitoring
    Network/                       # Wi-Fi, Time, MQTT, Home Assistant
    IOModule/                      # sensors/actuators, buses, drivers, endpoints
    PoolDeviceModule/              # pool equipment domain layer
    EventBusModule/                # event bus service host
    CommandModule/                 # command registry service
docs/
  en/                              # English documentation
  fr/                              # French documentation
  modules/                         # module documentation
```

## Available Modules

- Logs: `loghub`, `log.dispatcher`, `log.sink.serial`
- Core runtime: `eventbus`, `config`, `datastore`, `cmd`
- System: `system`, `sysmon`
- Network: `wifi`, `time`, `mqtt`, `ha`
- Pool domain: `io`, `pooldev`

## Complete Documentation

### Module Documentation (same structure for every module)

- [LogHubModule](docs/en/modules/LogHubModule.md)
- [LogDispatcherModule](docs/en/modules/LogDispatcherModule.md)
- [LogSerialSinkModule](docs/en/modules/LogSerialSinkModule.md)
- [EventBusModule](docs/en/modules/EventBusModule.md)
- [CommandModule](docs/en/modules/CommandModule.md)
- [ConfigStoreModule](docs/en/modules/ConfigStoreModule.md)
- [DataStoreModule](docs/en/modules/DataStoreModule.md)
- [SystemModule](docs/en/modules/SystemModule.md)
- [SystemMonitorModule](docs/en/modules/SystemMonitorModule.md)
- [WifiModule](docs/en/modules/WifiModule.md)
- [TimeModule](docs/en/modules/TimeModule.md)
- [MQTTModule](docs/en/modules/MQTTModule.md)
- [HAModule](docs/en/modules/HAModule.md)
- [IOModule](docs/en/IOModule.md)
- [PoolDeviceModule](docs/en/modules/PoolDeviceModule.md)

### Cross-Cutting Guides

- [CoreServicesGuidelines](docs/en/CoreServicesGuidelines.md)
- [ModuleDevGuide](docs/en/ModuleDevGuide.md)
- [Module Quality Gates](docs/en/QualityGates.md)
