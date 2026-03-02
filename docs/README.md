# Documentation développeur Flow.IO

Cette documentation couvre l'architecture complète du firmware Flow.IO (ESP32), les contrats Core, les flux de données/événements, la topologie MQTT, et le détail de chaque module.

## Sommaire

- [Architecture Core](core/architecture.md)
- [Services Core et invocation](core/services.md)
- [Modèle DataStore + EventBus + Config](core/data-event-model.md)
- [Topologie MQTT détaillée](core/mqtt-topics.md)
- [Quality Gates Modules (10 points)](core/module-quality-gates.md)

## Vue d'ensemble runtime

Flow.IO est basé sur:
- des modules actifs (`Module`) exécutés dans des tasks FreeRTOS dédiées
- des modules passifs (`ModulePassive`) qui exposent des services sans task
- un `ServiceRegistry` typé pour les dépendances inter-modules
- un `EventBus` queue-based pour la signalisation interne
- un `DataStore` pour l'état runtime partagé
- un `ConfigStore` pour la configuration persistante NVS + JSON

## Ordre de boot (main)

Ordre d'enregistrement dans `main.cpp`:
1. `loghub`
2. `log.dispatcher`
3. `log.sink.serial`
4. `eventbus`
5. `config`
6. `datastore`
7. `cmd`
8. `hmi`
9. `alarms`
10. `log.sink.alarm`
11. `wifi`
12. `time`
13. `mqtt`
14. `ha`
15. `system`
16. `io`
17. `poollogic`
18. `pooldev`
19. `sysmon`

Puis:
1. init de tous les modules (ordre topologique)
2. `ConfigStore::loadPersistent()`
3. `onConfigLoaded()`
4. démarrage des tasks des modules actifs

## Répartition CPU (ESP32)

- Core 0: `wifi`, `mqtt`, `ha`, `sysmon`
- Core 1: `eventbus`, `io`, `poollogic`, `pooldev`, `alarms`, `time`
- Modules passifs: pas de task (`hasTask=false`)

## Brochage ESP32 par firmware

Les tableaux ci-dessous résument les GPIO effectivement utilisés dans les profils compilés `FlowIO` et `Supervisor` (valeurs par défaut actuelles des sources/build flags).

### Flow.IO

| GPIO | Utilité | Module(s) | Remarques |
|---|---|---|---|
| 32 | Sortie digitale `filtration` | `io` / `pooldev` | `Board::DO::Filtration` |
| 25 | Sortie digitale `ph_pump` | `io` / `pooldev` | `Board::DO::PhPump` |
| 26 | Sortie digitale `chlorine_pump` | `io` / `pooldev` | `Board::DO::ChlorinePump` |
| 13 | Sortie digitale `chlorine_generator` (impulsion) | `io` / `pooldev` | `Board::DO::ChlorineGenerator` |
| 33 | Sortie digitale `robot` | `io` / `pooldev` | `Board::DO::Robot` |
| 27 | Sortie digitale `lights` | `io` / `pooldev` | `Board::DO::Lights` |
| 23 | Sortie digitale `fill_pump` | `io` / `pooldev` | `Board::DO::FillPump` |
| 4 | Sortie digitale `water_heater` | `io` / `pooldev` | `Board::DO::WaterHeater` |
| 34 | Entrée digitale `Pool Level` | `io` | `Board::DI::FlowSwitch` |
| 36 | Entrée digitale `pH Level` | `io` | `Board::DI::PhLevel` |
| 39 | Entrée digitale `Chlorine Level` | `io` | `Board::DI::ChlorineLevel` |
| 19 | Bus 1-Wire A (sonde température eau) | `io` | `Board::OneWire::BusA` |
| 18 | Bus 1-Wire B (sonde température air) | `io` | `Board::OneWire::BusB` |
| 21 | I2C principal SDA (ADS1115/PCF8574, bus 0) | `io` | `Board::I2C::Sda` |
| 22 | I2C principal SCL (ADS1115/PCF8574, bus 0) | `io` | `Board::I2C::Scl` |
| 12 | I2C interlink SDA (Flow.IO <-> Supervisor, bus 1) | `i2ccfg.server` | Pin configurable (`i2c/cfg/server/sda`), bus fixé à 1 |
| 14 | I2C interlink SCL (Flow.IO <-> Supervisor, bus 1) | `i2ccfg.server` | Pin configurable (`i2c/cfg/server/scl`), bus fixé à 1 |
| 16 | UART2 RX (HMI Nextion) | `hmi` | Peut devenir port logs si `FLOW_SWAP_LOG_HMI_SERIAL=1` |
| 17 | UART2 TX (HMI Nextion) | `hmi` | Peut devenir port logs si `FLOW_SWAP_LOG_HMI_SERIAL=1` |
| 1 / 3 | UART0 TX/RX (console logs par défaut) | `log.sink.serial` | USB/serial monitor |

### Supervisor

| GPIO | Utilité | Module(s) | Remarques |
|---|---|---|---|
| 21 | I2C interlink SDA (Supervisor -> Flow.IO, bus 1) | `i2ccfg.client` | Pin configurable (`i2c/cfg/client/sda`), bus fixé à 1 |
| 22 | I2C interlink SCL (Supervisor -> Flow.IO, bus 1) | `i2ccfg.client` | Pin configurable (`i2c/cfg/client/scl`), bus fixé à 1 |
| 16 | UART2 RX (bridge WebSerial et flash Flow.IO) | `webinterface`, `fwupdate` | Peut être redéfini via `FLOW_SUPERVISOR_WEBSERIAL_RX` |
| 17 | UART2 TX (bridge WebSerial et flash Flow.IO) | `webinterface`, `fwupdate` | Peut être redéfini via `FLOW_SUPERVISOR_WEBSERIAL_TX` |
| 25 | Contrôle `EN` du Flow.IO cible (reset/enable) | `fwupdate` | `FLOW_SUPERVISOR_FLOWIO_EN_PIN` |
| 26 | Contrôle `BOOT` du Flow.IO cible (mode bootloader) | `fwupdate` | `FLOW_SUPERVISOR_FLOWIO_BOOT_PIN` |
| 33 | UART Nextion RX (upload TFT) | `fwupdate` | `FLOW_SUPERVISOR_NEXTION_RX` / `NEXT_RX` |
| 32 | UART Nextion TX (upload TFT) | `fwupdate` | `FLOW_SUPERVISOR_NEXTION_TX` / `NEXT_TX` |
| 13 | Reboot matériel Nextion | `fwupdate` | `FLOW_SUPERVISOR_NEXTION_REBOOT` |
| 1 / 3 | UART0 TX/RX (console logs par défaut) | `log.sink.serial` | USB/serial monitor |

## Dossier modules (1 fichier par module)

- [LogHubModule](modules/LogHubModule.md)
- [LogDispatcherModule](modules/LogDispatcherModule.md)
- [LogSerialSinkModule](modules/LogSerialSinkModule.md)
- [LogAlarmSinkModule](modules/LogAlarmSinkModule.md)
- [EventBusModule](modules/EventBusModule.md)
- [ConfigStoreModule](modules/ConfigStoreModule.md)
- [DataStoreModule](modules/DataStoreModule.md)
- [CommandModule](modules/CommandModule.md)
- [SystemModule](modules/SystemModule.md)
- [SystemMonitorModule](modules/SystemMonitorModule.md)
- [HMIModule](modules/HMIModule.md)
- [AlarmModule](modules/AlarmModule.md)
- [WifiModule](modules/WifiModule.md)
- [TimeModule](modules/TimeModule.md)
- [MQTTModule](modules/MQTTModule.md)
- [HAModule](modules/HAModule.md)
- [IOModule](modules/IOModule.md)
- [PoolLogicModule](modules/PoolLogicModule.md)
- [PoolDeviceModule](modules/PoolDeviceModule.md)

Évaluation qualité transversale:

- [Quality Gates Modules (notes + description des 10 points)](core/module-quality-gates.md)

## Capacités de slots (profil Flow.IO)

Les valeurs ci-dessous correspondent au profil `Flow.IO` actuel (wiring et modules enregistrés dans `main_flowio.cpp`).

| Domaine | Slots pré-définis (max) | Slots utilisés actuellement | Variable/constante de capacité (fichier) |
|---|---:|---:|---|
| IOModule - entrées analogiques | 12 | 6 | `IOModule::MAX_ANALOG_ENDPOINTS` ([src/Modules/IOModule/IOModule.h](../src/Modules/IOModule/IOModule.h)) |
| IOModule - entrées digitales | 8 | 1 | `IOModule::MAX_DIGITAL_INPUTS` ([src/Modules/IOModule/IOModule.h](../src/Modules/IOModule/IOModule.h)) |
| IOModule - sorties digitales | 12 | 8 | `IOModule::MAX_DIGITAL_OUTPUTS` ([src/Modules/IOModule/IOModule.h](../src/Modules/IOModule/IOModule.h)) |
| IOModule - slots digitaux totaux | 20 | 9 | `IOModule::MAX_DIGITAL_SLOTS` ([src/Modules/IOModule/IOModule.h](../src/Modules/IOModule/IOModule.h)) |
| PoolDeviceModule - devices | 8 | 8 | `POOL_DEVICE_MAX` ([src/Modules/PoolDeviceModule/PoolDeviceModuleDataModel.h](../src/Modules/PoolDeviceModule/PoolDeviceModuleDataModel.h)) |
| HAModule - sensors | 24 | 20 | `HAModule::MAX_HA_SENSORS` ([src/Modules/Network/HAModule/HAModule.h](../src/Modules/Network/HAModule/HAModule.h)) |
| HAModule - binary sensors | 8 | 0 | `HAModule::MAX_HA_BINARY_SENSORS` ([src/Modules/Network/HAModule/HAModule.h](../src/Modules/Network/HAModule/HAModule.h)) |
| HAModule - switches | 16 | 12 | `HAModule::MAX_HA_SWITCHES` ([src/Modules/Network/HAModule/HAModule.h](../src/Modules/Network/HAModule/HAModule.h)) |
| HAModule - numbers | 16 | 13 | `HAModule::MAX_HA_NUMBERS` ([src/Modules/Network/HAModule/HAModule.h](../src/Modules/Network/HAModule/HAModule.h)) |
| HAModule - buttons | 8 | 3 | `HAModule::MAX_HA_BUTTONS` ([src/Modules/Network/HAModule/HAModule.h](../src/Modules/Network/HAModule/HAModule.h)) |
| MQTTModule - runtime publishers | 8 | 4 | `Limits::Mqtt::Capacity::MaxPublishers` ([include/Core/SystemLimits.h](../include/Core/SystemLimits.h)) |
| Routes runtime multiplexées | 34 | 33 | `Limits::MaxRuntimeRoutes` ([include/Core/SystemLimits.h](../include/Core/SystemLimits.h)) |
| ConfigStore - variables de config | 256 | 253 | `Limits::MaxConfigVars` / `ConfigStore::MAX_CONFIG_VARS` ([include/Core/SystemLimits.h](../include/Core/SystemLimits.h), [src/Core/ConfigStore.h](../src/Core/ConfigStore.h)) |

Notes:
- Le compteur "utilisés actuellement" reflète l'état de la configuration/module wiring actuel de Flow.IO.
- Certaines allocations bas niveau (ex: drivers DS18B20) dépendent de la détection matérielle au boot et peuvent varier selon la carte branchée.

## Notes importantes

- Il n'existe pas de "EventStore" persistant dédié: les événements internes transitent dans `EventBus` (volatile en RAM).
- La persistance durable est assurée par `ConfigStore` (NVS).
- Les métriques runtime sont partagées via `DataStore` et exportées via MQTT.
