# Flow.io ESP32 Modular Framework

Framework C++17 pour ESP32 sous PlatformIO/Arduino + FreeRTOS. Le firmware est structuré en modules isolés, avec services typés, configuration persistante et DataStore runtime.

## Points clés
- C++17 + PlatformIO + FreeRTOS
- 1 task FreeRTOS par module actif
- Dépendances explicites et ordre d’init automatique
- Registre de services typés (`ServiceRegistry`)
- Configuration persistante NVS (`ConfigStore` + `Preferences`)
- Import/Export JSON de config
- EventBus avec payloads fixes
- Logging centralisé (hub + sinks)
- DataStore runtime + dirty flags + notifications
- MQTT: commandes, config, et snapshots runtime

## Arborescence
```
src/
  main.cpp
  Core/
    Module.h / ModuleManager.h
    ServiceRegistry.h
    ConfigStore.h / ConfigTypes.h / ConfigMigrations.h
    EventBus/
    DataStore/
    LogHub.h / LogSinkRegistry.h
    SystemStats.h
    Services/ (interfaces de services)
  Modules/
    Logs/ (LogHub, LogDispatcher, LogSerial)
    EventBusModule/
    CommandModule/
    Stores/ (ConfigStoreModule, DataStoreModule)
    System/ (SystemModule, SystemMonitorModule)
    Network/ (WifiModule, NTPModule, MQTTModule)
    IOModule/ (IOBus, IODrivers, IOEndpoints, IORegistry, IOScheduler)
platformio.ini
```

## Modules (actuels)
- **Logs**: `LogHubModule`, `LogDispatcherModule`, `LogSerialSinkModule`
- **Core**: `EventBusModule`, `CommandModule`, `ConfigStoreModule`, `DataStoreModule`
- **System**: `SystemModule`, `SystemMonitorModule`
- **Network**: `WifiModule`, `NTPModule`, `MQTTModule`
- **I/O**: `IOModule`

## MQTT (résumé)
Topics racine: `flowio/<deviceId>/...`
- `cmd`, `ack`, `status`
- `cfg/set`, `cfg/ack`, `cfg/<module>`
- `rt/io/state` (snapshot JSON)
- `rt/network/state` (snapshot JSON)
- `rt/system/state` (snapshot JSON)

Spécification détaillée: [MQTTModule](docs/MQTTModule.md)

## Documentation
- [DevGuide](docs/DevGuide.md)
- [IOModule](docs/IOModule.md)
- [MQTTModule](docs/MQTTModule.md)

## Développement
- Génération modèle runtime: `scripts/generate_datamodel.py`
- Exemple complet de module: [DevGuide](docs/DevGuide.md)

## Build
```
pio run
```

## Notes
- Les modèles runtime sont agrégés via `ModuleDataModel_Generated.h`.
- Les helpers runtime sont agrégés via `ModuleRuntime_Generated.h`.
