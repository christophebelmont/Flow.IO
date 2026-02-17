# Modèle DataStore, EventBus et Config

## Introduction

Flow.IO sépare volontairement 3 responsabilités:
1. `ConfigStore`: vérité persistante (NVS), modifiable par patch JSON.
2. `DataStore`: vérité runtime en RAM (état vivant du système).
3. `EventBus`: signalisation asynchrone entre modules.

Cette séparation évite de mélanger:
- ce qui doit survivre au reboot (`ConfigStore`)
- ce qui change en continu (`DataStore`)
- la façon d’annoncer ces changements (`EventBus`)

## Les “registres” à connaître

Quand on parle de registres dans ce firmware, il faut distinguer les registres de routage, pas des registres CPU.

### 1) Registre de configuration (`ConfigStore` + `ConfigMeta`)

Chaque variable config est enregistrée avec:
- `module` JSON (ex: `mqtt`, `poollogic`, `pdm/pd1`)
- `jsonName` (ex: `sens_min_pub_ms`)
- `nvsKey` (ex: `mq_smin`)
- `moduleId` + `branchId` (routage des événements `ConfigChanged`)
- type et mode persistant/volatile

Utilité:
- convertir JSON <-> variables C++
- persister automatiquement en NVS
- notifier précisément quel bloc config a changé

### 2) Registre runtime (`DataStore` + `RuntimeData`)

`RuntimeData` est agrégé depuis `src/Core/Generated/ModuleDataModel_Generated.h`:
- `wifi`
- `time`
- `mqtt`
- `ha`
- `io`
- `pool`

Utilité:
- point central de lecture runtime pour tous les modules
- écriture via helpers typés (`WifiRuntime.h`, `IORuntime.h`, `PoolDeviceRuntime.h`, etc.)

### 3) Registre de clés runtime (`DataKeys`)

`DataKey` est un identifiant stable de champ runtime utilisé dans `DataChangedPayload`.

Clés fixes:
- `1` `WifiReady`
- `2` `WifiIp`
- `3` `TimeReady`
- `4` `MqttReady`
- `5` `MqttRxDrop`
- `6` `MqttParseFail`
- `7` `MqttHandlerFail`
- `8` `MqttOversizeDrop`
- `10` `HaPublished`
- `11` `HaVendor`
- `12` `HaDeviceId`

Plages réservées:
- `40..63` IO endpoints
- `80..87` pool-device state
- `88..95` pool-device metrics

Utilité:
- décoder rapidement quel sous-système a changé sans parser un gros objet

### 4) Registre d’événements (`EventId` + payloads)

`EventId` définit le type d’événement, les structures de payload dans `EventPayloads.h` définissent le contrat binaire.

Événements majeurs:
- `DataChanged`
- `DataSnapshotAvailable`
- `ConfigChanged`
- `SchedulerEventTriggered`
- `AlarmRaised`/`Cleared`/`Acked`/`SilenceChanged`/`ConditionChanged`

Utilité:
- découpler producteurs et consommateurs
- transporter un delta petit, stable et déterministe

### 5) Registre de branches config (`ConfigBranchIds`)

`branchId` mappe chaque zone config à un topic logique (`cfg/<module>`).

Exemples:
- `Mqtt` -> `cfg/mqtt`
- `TimeScheduler` -> `cfg/time/scheduler`
- `PoolDevicePd0` -> `cfg/pdm/pd0`
- `PoolDeviceRuntimePd0` -> `cfg/pdmrt/pd0`

Utilité:
- publication MQTT ciblée au lieu de republier toute la config

## DataStore: mécanique exacte

### Contrat

Toute écriture runtime suit ce pattern:
1. module écrit dans `RuntimeData` via helper `set...`.
2. helper appelle `DataStore::notifyChanged(key, dirtyMask)`.
3. `notifyChanged()` fait 3 actions: OR du `dirtyMask` dans `_dirtyFlags`, `post(DataChanged, DataChangedPayload{key})`, puis `post(DataSnapshotAvailable, DataSnapshotPayload{dirtyFlags})`.

### Dirty flags

Flags:
- `DIRTY_NETWORK`
- `DIRTY_TIME`
- `DIRTY_MQTT`
- `DIRTY_SENSORS`
- `DIRTY_ACTUATORS`

Ces flags servent de “classe de changement”, pas d’identifiant fin.

### Point important

Dans l’état actuel du code, `_dirtyFlags` est cumulatif tant que personne n’appelle `consumeDirtyFlags()` (méthode présente mais non utilisée).

Conséquence:
- `DataSnapshotAvailable` est un hint de type “au moins ces classes ont changé”, pas un delta strict “depuis le dernier publish MQTT”.

## EventBus: mécanique exacte

### File et dispatch

- queue FreeRTOS longueur `16` (`Limits::EventQueueLen`)
- payload max `48` bytes
- `post()` non bloquant (`xQueueSend(..., 0)`)
- dispatch dans `EventBusModule` avec batch de `8` événements par tour
- période de loop `5ms`

### Implication pratique

Si la queue est pleine, `post()` retourne `false`.
Certains producteurs ignorent ce retour.
Le design privilégie la non-blocage du temps réel, au prix d’une perte possible d’événement en surcharge.

### Qui publie quoi

- `DataStore` -> `DataChanged`, `DataSnapshotAvailable`
- `ConfigStore` -> `ConfigChanged`
- `TimeModule` -> `SchedulerEventTriggered`
- `AlarmModule` -> événements d’alarme
- `EventBusModule` -> `SystemStarted`

### Qui consomme quoi (principaux)

- `MQTTModule`: `DataChanged` (clé `WifiReady`), `DataSnapshotAvailable`, `ConfigChanged`, événements alarme
- `TimeModule`: `DataChanged` (`WifiReady`), `ConfigChanged` (`Time`, `TimeScheduler`)
- `PoolDeviceModule`: `SchedulerEventTriggered`, `DataChanged` (`TimeReady`), `ConfigChanged` (`Time`)
- `PoolLogicModule`: `SchedulerEventTriggered`

## ConfigStore: cycle de vie d’un changement

### Boot

1. modules enregistrent leurs `ConfigVariable`.
2. `ConfigStore::loadPersistent()` hydrate depuis NVS.
3. `onConfigLoaded()` permet aux modules d’ajuster leur runtime.

### Patch JSON (`cfg/set`)

Flux:
1. MQTT reçoit patch JSON.
2. `ConfigStore::applyJson()` met à jour variables concernées.
3. chaque variable persistante modifiée est écrite NVS.
4. pour chaque clé modifiée, `ConfigStore` publie `ConfigChanged` avec `moduleId`, `branchId`, `nvsKey`, `module`.

Ce payload permet aux consommateurs (surtout MQTT) de republier seulement le bon bloc `cfg/...`.

## End-to-end: exemples de flux

### Exemple A: nouvelle mesure capteur (IO analogique)

1. `IOModule` lit la valeur.
2. `setIoEndpointFloat(..., DIRTY_SENSORS)`.
3. `DataStore` publie `DataChanged` + `DataSnapshotAvailable`.
4. `MQTTModule` reçoit `DataSnapshotAvailable`, pose `sensorsPending=true` et OR le mask dans `sensorsPendingDirtyMask`.
5. boucle MQTT déclenche callback mux runtime (`publishRuntimeStates`).
6. callback publie seulement les routes pertinentes/nécessaires.

### Exemple B: commande actionneur

1. `cmd` MQTT -> `pooldevice.write`.
2. `PoolDeviceModule` met à jour état désiré/réel.
3. `setPoolDeviceRuntimeState(..., DIRTY_ACTUATORS)`.
4. `MQTTModule` déclenche un publish actuator prioritaire même en fenêtre de throttle (voir section suivante).

### Exemple C: changement config

1. `cfg/set` MQTT.
2. `ConfigStore::applyJson`.
3. `ConfigChanged(branchId=...)`.
4. `MQTTModule` queue une republication ciblée de `cfg/<module>`.

## Throttling: comment c’est réellement géré

Le throttling est multi-couches.

### Couche 1: anti-bruit à la source (setters runtime)

Les helpers `set...` évitent de notifier si la valeur n’a pas changé.
Exemple: `setMqttReady`, `setIoEndpointBool`, `setPoolDeviceRuntimeMetrics`.

Effet:
- pas d’événement inutile quand la donnée est identique

### Couche 2: filtrage par classe de changement

Le callback runtime mux (`main.cpp`) reçoit `activeSensorsDirtyMask` depuis MQTT.
Chaque route possède un `dirtyMask`:
- `rt/io/output/*` et `rt/pdm/state/*` -> `DIRTY_ACTUATORS`
- `rt/io/input/*` et `rt/pdm/metrics/*` -> `DIRTY_SENSORS`

Effet:
- un changement capteur ne force pas le rebuild de toutes les routes actionneur, et inversement

### Couche 3: fenêtre minimum MQTT (`sens_min_pub_ms`)

Config: `mqtt.sens_min_pub_ms` (défaut `20000ms`).

Comportement:
- si on est dans la fenêtre throttle et qu’il y a des actionneurs dirty, publish actionneurs autorisé immédiatement
- les capteurs peuvent attendre la fin de fenêtre
- hors fenêtre, publish normal des dirty flags actifs

Spécial reconnect/boot:
- pendant `StartupActuatorRetryMs` (`3000ms`), les routes actionneurs sont republiées même si déjà envoyées, pour recoller vite l’état HA.

### Couche 4: anti-duplicate par timestamp route

Dans `publishRuntimeStates`, chaque route garde `lastPublishedTs`.
Si `ts <= lastPublishedTs`, route skip (`routes_skip_nc`).

Effet:
- même si dirty flags restent larges, une route sans vrai nouveau timestamp n’est pas republiée

### Couche 5: publishers périodiques séparés

Indépendamment des dirty flags, des snapshots périodiques existent:
- `rt/runtime/state` toutes 30s
- `rt/network/state` toutes 60s
- `rt/system/state` toutes 60s

Effet:
- observabilité continue même sans changement capteur/actionneur

## Comment tuner le throttling

1. Diminuer `mqtt.sens_min_pub_ms` pour plus de réactivité capteurs.
2. Augmenter `mqtt.sens_min_pub_ms` pour réduire trafic CPU/radio.
3. Garder en tête que les actionneurs sont priorisés pendant throttle.
4. Surveiller `rt/runtime/state` avec `routes_pub`, `routes_skip_nc`, `routes_skip_m`, `build_errors`, `publish_errors`.

## EventStore: ce qui existe et ce qui n’existe pas

Il n’existe pas de journal d’événements persistant dédié.

- `EventBus`: RAM, volatile, temps réel, potentiellement pertes sous surcharge
- `ConfigStore`: durable (NVS), source de vérité de la configuration
- `DataStore`: RAM, état courant partagé, non durable
