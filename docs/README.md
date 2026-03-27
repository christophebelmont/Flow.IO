# Documentation Flow.IO

Cette documentation est organisée pour deux usages distincts:

- mise en service et adaptation légère par un intégrateur technique
- référence technique descriptive de l'implémentation actuelle

Le projet est prévu pour fonctionner avec deux ESP32 distincts, chacun avec son propre firmware:

- `FlowIO`: ESP32 principal qui exécute la logique métier et pilote les entrées/sorties
- `Supervisor`: ESP32 de supervision qui configure le système, assure le provisioning Wi-Fi, gère l'écran TFT, permet la consultation des logs et pilote les mises à jour

Le projet compile aujourd'hui deux firmwares ESP32:

| Firmware | Environnement PlatformIO | Rôle |
|---|---|---|
| `FlowIO` | `FlowIO` | ESP32 principal: logique métier, entrées/sorties, MQTT, Home Assistant, écran Nextion |
| `Supervisor` | `Supervisor` | ESP32 de supervision: configuration, provisioning Wi-Fi, écran TFT, consultation des logs, mises à jour, passerelle I2C vers `FlowIO` |

## Parcours de lecture

### Mise en service et adaptation

- [Mise en service matérielle et flash](integration/mise-en-service.md)
- [Adapter le projet à un autre domaine](integration/adaptation-domaine.md)

### Référence technique

- [Architecture générale](core/architecture.md)
- [Structure des profils, cartes, domaines et bootstrap](core/profiles-board-domain-app.md)
- [Services Core](core/services.md)
- [Modèle `ConfigStore` / `DataStore` / `EventBus` / MQTT](core/data-event-model.md)
- [Topologie MQTT](core/mqtt-topics.md)
- [Protocole I2C `FlowIO` <-> `Supervisor`](core/flow-supervisor-i2c-protocol.md)
- [Exposition Runtime UI](core/runtime-ui-exposure.md)
- [Empreinte mémoire `FlowIO`](core/memory-footprint-flowio.md)
- [Matrice des modules](core/module-quality-gates.md)
- [Schéma d'ensemble du programme](program_structure.md)

### Référence par module

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
- [SupervisorHMIModule](modules/SupervisorHMIModule.md)
- [AlarmModule](modules/AlarmModule.md)
- [WifiModule](modules/WifiModule.md)
- [TimeModule](modules/TimeModule.md)
- [MQTTModule](modules/MQTTModule.md)
- [HAModule](modules/HAModule.md)
- [IOModule](modules/IOModule.md)
- [PoolLogicModule](modules/PoolLogicModule.md)
- [PoolDeviceModule](modules/PoolDeviceModule.md)

## Brochage actuel

Les tableaux ci-dessous décrivent le câblage actuellement reflété par les sources du dépôt.

### Flow.IO

Références principales:

- `src/Board/FlowIOBoardRev1.h`
- `src/Board/BoardSerialMap.h`
- `src/Modules/Network/I2CCfgServerModule/I2CCfgServerModule.h`

| GPIO | Usage | Remarque |
|---|---|---|
| 32 | relais `relay1` | sortie digitale, rôle par défaut `FiltrationPump` |
| 25 | relais `relay2` | sortie digitale, rôle par défaut `PhPump` |
| 26 | relais `relay3` | sortie digitale, rôle par défaut `ChlorinePump` |
| 13 | relais `relay4` | sortie digitale impulsionnelle, rôle par défaut `ChlorineGenerator` |
| 33 | relais `relay5` | sortie digitale, rôle par défaut `Robot` |
| 27 | relais `relay6` | sortie digitale, rôle par défaut `Lights` |
| 23 | relais `relay7` | sortie digitale, rôle par défaut `FillPump` |
| 4 | relais `relay8` | sortie digitale, rôle par défaut `WaterHeater` |
| 34 | `digital_in1` | entrée digitale, rôle par défaut `PoolLevelSensor` |
| 36 | `digital_in2` | entrée digitale, rôle par défaut `PhLevelSensor` |
| 39 | `digital_in3` | entrée digitale, rôle par défaut `ChlorineLevelSensor` |
| 35 | `digital_in4` | entrée digitale, rôle par défaut `WaterCounterSensor` |
| 19 | `temp_probe_1` | bus 1-Wire, rôle par défaut `WaterTemp` |
| 18 | `temp_probe_2` | bus 1-Wire, rôle par défaut `AirTemp` |
| 21 | I2C `io` SDA | bus principal des ADS1115 et du PCF8574 |
| 22 | I2C `io` SCL | bus principal des ADS1115 et du PCF8574 |
| 12 | I2C interlink SDA | valeur par défaut du serveur `i2c/cfg/server` |
| 14 | I2C interlink SCL | valeur par défaut du serveur `i2c/cfg/server` |
| 16 | UART2 RX | interface Nextion par défaut |
| 17 | UART2 TX | interface Nextion par défaut |
| 1 / 3 | UART0 | console série par défaut |

### Supervisor

Références principales:

- `src/Board/SupervisorBoardRev1.h`
- `src/Profiles/Supervisor/SupervisorProfile.cpp`
- `src/Modules/Network/I2CCfgClientModule/I2CCfgClientModule.h`

| GPIO | Usage | Remarque |
|---|---|---|
| 21 | I2C interlink SDA | client `i2c/cfg/client` vers `FlowIO` |
| 22 | I2C interlink SCL | client `i2c/cfg/client` vers `FlowIO` |
| 16 | UART `bridge` RX | pont série vers `FlowIO` |
| 17 | UART `bridge` TX | pont série vers `FlowIO` |
| 33 | UART `panel` RX | liaison série Nextion côté Supervisor |
| 32 | UART `panel` TX | liaison série Nextion côté Supervisor |
| 25 | `flowIoEnablePin` | activation/reset matériel du `FlowIO` cible |
| 26 | `flowIoBootPin` | entrée mode bootloader du `FlowIO` cible |
| 13 | `nextionRebootPin` | redémarrage matériel Nextion |
| 14 | TFT ST7789 backlight | écran local Supervisor |
| 15 | TFT ST7789 CS | écran local Supervisor |
| 2 | TFT ST7789 DC | écran local Supervisor |
| 4 | TFT ST7789 RST | écran local Supervisor |
| 23 | TFT ST7789 MOSI/SDA | écran local Supervisor |
| 18 | TFT ST7789 SCLK/SCL | écran local Supervisor |
| 36 | entrée PIR | extinction automatique du backlight et rallumage sur détection de présence |
| 1 / 3 | UART0 | console série par défaut |

Sur le profil `Supervisor`, le reset Wi-Fi par appui long est prévu sur l'entrée `wifiResetPin`. Dans l'état actuel de la carte décrite par `src/Board/SupervisorBoardRev1.h`, cette entrée est désactivée (`pin = -1`), donc aucun bouton matériel de reset Wi‑Fi n'est câblé par défaut.

## Composition actuelle des firmwares

### Profil `FlowIO`

Ordre d'enregistrement dans `src/Profiles/FlowIO/FlowIOBootstrap.cpp`:

1. `loghub`
2. `log.dispatcher`
3. `log.sink.serial`
4. `eventbus`
5. `config`
6. `datastore`
7. `cmd`
8. `i2ccfg.server`
9. `hmi`
10. `alarms`
11. `log.sink.alarm`
12. `wifi`
13. `time`
14. `mqtt`
15. `ha`
16. `system`
17. `io`
18. `poollogic`
19. `pooldev`
20. `sysmon`

### Profil `Supervisor`

Ordre d'enregistrement dans `src/Profiles/Supervisor/SupervisorBootstrap.cpp`:

1. `loghub`
2. `log.dispatcher`
3. `log.sink.serial`
4. `eventbus`
5. `config`
6. `datastore`
7. `cmd`
8. `alarms`
9. `log.sink.alarm`
10. `wifi`
11. `wifiprov`
12. `time`
13. `i2ccfg.client`
14. `webinterface`
15. `fwupdate`
16. `hmi.supervisor`
17. `system`
18. `sysmon`

## Capacités statiques utiles à l'intégration

Les valeurs ci-dessous correspondent à l'implémentation actuelle du profil `FlowIO`.

| Domaine | Capacité compile-time | Implémentation |
|---|---:|---|
| Entrées analogiques IO | 12 | `IOModule::MAX_ANALOG_ENDPOINTS` |
| Entrées digitales IO | 5 | `IOModule::MAX_DIGITAL_INPUTS` |
| Sorties digitales IO | 10 | `IOModule::MAX_DIGITAL_OUTPUTS` |
| Équipements `PoolDevice` | 8 | `POOL_DEVICE_MAX` |
| Capteurs Home Assistant | 24 | `HAModule::MAX_HA_SENSORS` |
| Binary sensors Home Assistant | 8 | `HAModule::MAX_HA_BINARY_SENSORS` |
| Switches Home Assistant | 16 | `HAModule::MAX_HA_SWITCHES` |
| Numbers Home Assistant | 16 | `HAModule::MAX_HA_NUMBERS` |
| Buttons Home Assistant | 8 | `HAModule::MAX_HA_BUTTONS` |
| Routes runtime MQTT* | 36 | `Limits::MaxRuntimeRoutes` |
| EventBus queue | 32 | `Limits::EventQueueLen` |
| Variables de configuration | 315 | `Limits::MaxConfigVars` |

\* Une route runtime MQTT correspond à un canal de publication runtime déclaré dans le firmware. Chaque route relie une source d'état interne à un suffixe de topic MQTT, par exemple `rt/io/input/a0`, `rt/pdm/state/pd0` ou `rt/system/state`.
