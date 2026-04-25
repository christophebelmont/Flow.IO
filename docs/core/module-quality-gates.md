# Matrice des modules

Cette page récapitule les modules documentés dans `docs/modules/`, leur rôle général et leur présence dans les profils actuellement compilés.

## Modules communs aux deux profils

| Module | `moduleId` | Type | Profil `FlowIO` | Profil `Supervisor` | Documentation |
|---|---|---|---|---|---|
| LogHubModule | `loghub` | passif | oui | oui | `docs/modules/LogHubModule.md` |
| LogDispatcherModule | `log.dispatcher` | passif avec tâche dédiée | oui | oui | `docs/modules/LogDispatcherModule.md` |
| LogSerialSinkModule | `log.sink.serial` | passif | oui | oui | `docs/modules/LogSerialSinkModule.md` |
| EventBusModule | `eventbus` | actif | oui | oui | `docs/modules/EventBusModule.md` |
| ConfigStoreModule | `config` | passif | oui | oui | `docs/modules/ConfigStoreModule.md` |
| DataStoreModule | `datastore` | passif | oui | oui | `docs/modules/DataStoreModule.md` |
| CommandModule | `cmd` | passif | oui | oui | `docs/modules/CommandModule.md` |
| AlarmModule | `alarms` | actif | oui | oui | `docs/modules/AlarmModule.md` |
| WifiModule | `wifi` | actif | oui | oui | `docs/modules/WifiModule.md` |
| TimeModule | `time` | actif | oui | oui | `docs/modules/TimeModule.md` |
| SystemModule | `system` | passif | oui | oui | `docs/modules/SystemModule.md` |
| SystemMonitorModule | `sysmon` | actif | oui | oui | `docs/modules/SystemMonitorModule.md` |

## Modules spécifiques à `FlowIO`

| Module | `moduleId` | Type | Rôle | Documentation |
|---|---|---|---|---|
| HMIModule | `hmi` | actif | interface locale Nextion | `docs/modules/HMIModule.md` |
| MQTTModule | `mqtt` | actif | transport MQTT et producteurs | `docs/modules/MQTTModule.md` |
| HAModule | `ha` | actif | discovery Home Assistant | `docs/modules/HAModule.md` |
| IOModule | `io` | actif | entrées/sorties, drivers, snapshots runtime | `docs/modules/IOModule.md` |
| PoolLogicModule | `poollogic` | actif | logique métier piscine | `docs/modules/PoolLogicModule.md` |
| PoolDeviceModule | `pooldev` | actif | pilotage des équipements | `docs/modules/PoolDeviceModule.md` |

## Modules spécifiques à `Supervisor`

| Module | `moduleId` | Type | Rôle | Documentation |
|---|---|---|---|---|
| LogAlarmSinkModule | `log.sink.alarm` | passif | conversion de logs Warn/Error en conditions d'alarme | `docs/modules/LogAlarmSinkModule.md` |
| `i2ccfg.client` | `i2ccfg.client` | passif avec tâche | accès distant à la configuration et au runtime du `FlowIO` | `docs/core/flow-supervisor-i2c-protocol.md` |
| `wifiprov` | `wifiprov` | actif | provisioning réseau local | `docs/integration/mise-en-service.md` |
| `webinterface` | `webinterface` | actif | interface web Supervisor | `docs/core/runtime-ui-exposure.md` |
| `fwupdate` | `fwupdate` | actif | mise à jour firmware et écran | `docs/integration/mise-en-service.md` |
| SupervisorHMIModule | `hmi.supervisor` | actif | interface TFT locale | `docs/modules/SupervisorHMIModule.md` |

## Modules complémentaires utilisés par les profils

| Module | Profil | Rôle |
|---|---|---|
| `i2ccfg.server` | `FlowIO` | serveur I2C de configuration et Runtime UI |
| `i2ccfg.client` | `Supervisor` | client I2C de configuration et Runtime UI |

## Ordre d'enregistrement

L'ordre d'enregistrement réel des modules est défini par les fichiers:

- `src/Profiles/FlowIO/FlowIOBootstrap.cpp`
- `src/Profiles/Supervisor/SupervisorBootstrap.cpp`

Cet ordre pilote:

- le tri topologique
- l'initialisation
- le démarrage des tâches
