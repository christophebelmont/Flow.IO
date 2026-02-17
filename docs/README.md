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
8. `alarms`
9. `wifi`
10. `time`
11. `mqtt`
12. `ha`
13. `system`
14. `io`
15. `poollogic`
16. `pooldev`
17. `sysmon`

Puis:
1. init de tous les modules (ordre topologique)
2. `ConfigStore::loadPersistent()`
3. `onConfigLoaded()`
4. démarrage des tasks des modules actifs

## Répartition CPU (ESP32)

- Core 0: `wifi`, `mqtt`, `ha`, `sysmon`
- Core 1: `eventbus`, `io`, `poollogic`, `pooldev`, `alarms`, `time`
- Modules passifs: pas de task (`hasTask=false`)

## Dossier modules (1 fichier par module)

- [LogHubModule](modules/LogHubModule.md)
- [LogDispatcherModule](modules/LogDispatcherModule.md)
- [LogSerialSinkModule](modules/LogSerialSinkModule.md)
- [EventBusModule](modules/EventBusModule.md)
- [ConfigStoreModule](modules/ConfigStoreModule.md)
- [DataStoreModule](modules/DataStoreModule.md)
- [CommandModule](modules/CommandModule.md)
- [SystemModule](modules/SystemModule.md)
- [SystemMonitorModule](modules/SystemMonitorModule.md)
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

## Notes importantes

- Il n'existe pas de "EventStore" persistant dédié: les événements internes transitent dans `EventBus` (volatile en RAM).
- La persistance durable est assurée par `ConfigStore` (NVS).
- Les métriques runtime sont partagées via `DataStore` et exportées via MQTT.
