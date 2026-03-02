# DataStore et Flux Runtime (état actuel)

## Introduction

Flow.IO sépare les responsabilités en 4 blocs:
1. `ConfigStore`: configuration persistante (NVS), chargée au boot puis modifiable.
2. `DataStore`: état runtime en RAM (`RuntimeData`).
3. `EventBus`: signalisation asynchrone (`EventId`, payloads bornés à 48 octets).
4. `RuntimeDispatchModule`: décision de publication runtime MQTT (throttle, immédiat, retry).

Ce document décrit le **flux réellement implémenté** après le refactor big-bang runtime.

## Structures et contrats clés

- `RuntimeData` (agrégat): `wifi`, `time`, `mqtt`, `ha`, `io`, `pool`.
- `DataKey` (`include/Core/DataKeys.h`): identifiants stables des champs runtime.
- `DataChangedPayload`:
  - `DataKey id`
  - `uint32_t dirtyFlags`
- `DirtyFlags`:
  - `DIRTY_NETWORK`, `DIRTY_TIME`, `DIRTY_MQTT`, `DIRTY_SENSORS`, `DIRTY_ACTUATORS`.
- `IRuntimeSnapshotProvider`:
  - `runtimeSnapshotCount()`
  - `runtimeSnapshotSuffix()`
  - `runtimeSnapshotClass()`
  - `runtimeSnapshotAffectsKey()`
  - `buildRuntimeSnapshot()`
- `RuntimeRouteClass`:
  - `NumericThrottled`
  - `ActuatorImmediate`

## DataStore: mécanique exacte

Écriture runtime standard:
1. un module met à jour `RuntimeData` via un helper `set...`.
2. le helper appelle `DataStore::notifyChanged(key, dirtyMask)`.
3. `DataStore::notifyChanged()` publie `EventId::DataChanged` avec `DataChangedPayload{id, dirtyFlags}`.

Important:
- `DataSnapshotAvailable` n'est plus utilisé.
- il n'y a plus de `_dirtyFlags` cumulatif dans `DataStore`.

## Flowchart 1: enregistrements au démarrage

```mermaid
flowchart TD
A["main_flowio::setup()"] --> B["moduleManager.add(...)\n(inclut runtimeDispatchModule)"]
B --> C["runtimeDispatchModule.registerProvider(io/pooldev/poollogic)"]
C --> D["ModuleManager::initAll(cfg, services)"]

D --> E["Phase init() de chaque module"]
E --> E1["DataStoreModule::init\nsetEventBus + services.add('datastore')"]
E --> E2["EventBusModule::init\nservices.add('eventbus')"]
E --> E3["MQTTModule::init\nservices.add('mqtt')"]
E --> E4["IOModule::init\nregisterVar + services.add('io')"]
E --> E5["PoolDeviceModule::init\nservices.add('pooldev')"]
E --> E6["RuntimeDispatchModule::init\nsubscribe(DataChanged)"]

D --> F["ConfigStore::loadPersistent()"]
F --> G["Phase onConfigLoaded()"]
G --> G1["IOModule::onConfigLoaded\nconfigureRuntime_()"]
G --> G2["PoolDeviceModule::onConfigLoaded\nloadPersistedMetrics_()"]
G --> G3["PoolLogicModule::onConfigLoaded\nensureDailySlot_()"]
G --> G4["RuntimeDispatchModule::onConfigLoaded\nrebuildRoutes_ + markAllRoutesDirty(true)"]

D --> H["startTask() modules actifs"]
H --> H1["EventBusModule::loop\nEventBus::dispatch(8)"]
H --> H2["RuntimeDispatchModule::loop\n(25 ms)"]
H --> H3["IOModule::loop / PoolDeviceModule::loop / MQTTModule::loop"]
```

### Fonctions impliquées (startup)

- `main_flowio::setup()`
- `ModuleManager::initAll()`
- `ConfigStore::loadPersistent()`
- `Module::init()` / `Module::onConfigLoaded()` / `Module::startTask()`
- `RuntimeDispatchModule::registerProvider()`
- `RuntimeDispatchModule::rebuildRoutes_()`
- `RuntimeDispatchModule::markAllRoutesDirty(true)`

### Structures manipulées (startup)

- `ServiceRegistry`
- `ConfigStore` / `ConfigVariable<>`
- `RuntimeDispatchModule::providers_[]`
- `RuntimeDispatchModule::routes_[]` (`RouteEntry`)
- `EventBus` (`QueuedEvent`)

## Flowchart 2: changement d'un état numérique runtime

Exemple typique: mesure analogique (`rt/io/input/aN`).

```mermaid
flowchart TD
A["IOModule::loop()"] --> B["IOModule::ioTick_(now)"]
B --> C["scheduler_.tick(now)"]
C --> D["tickFastAds_/tickSlowDs_"]
D --> E["IOModule::processAnalogDefinition_(idx)"]

E --> F["slot.endpoint->update(value,true,ts)"]
F --> G["callback def.onValueChanged(ctx,value)"]
G --> H["main_flowio::onIoFloatValue(ctx,value)"]
H --> I["setIoEndpointFloat(ds, idx, value, millis, DIRTY_SENSORS)"]

I --> J["RuntimeData.io.endpoints[idx] = IOEndpointRuntime"]
J --> K["DataStore::notifyChanged(DataKey=IoBase+idx, DIRTY_SENSORS)"]
K --> L["EventBus::post(EventId::DataChanged, DataChangedPayload)"]

L --> M["EventBus::dispatch() -> RuntimeDispatchModule::onEvent_()"]
M --> N["provider.runtimeSnapshotAffectsKey(...) ? markRouteDirty(i,false)"]
N --> O["RuntimeDispatchModule::loop()"]
O --> P{"routeClass == NumericThrottled ?"}
P -->|oui| Q["throttle 10s (kNumericThrottleMs)"]
Q --> R["provider.buildRuntimeSnapshot(...)"]
P -->|non| R
R --> S["mqttSvc.publish -> MQTTModule::publish -> esp_mqtt_client_publish"]
```

### Fonctions impliquées (numérique)

- `IOModule::processAnalogDefinition_()`
- `main_flowio::onIoFloatValue()`
- `setIoEndpointFloat()`
- `DataStore::notifyChanged()`
- `EventBus::post()`
- `RuntimeDispatchModule::onEvent_()`
- `RuntimeDispatchModule::markRouteDirty_()`
- `RuntimeDispatchModule::loop()`
- `IRuntimeSnapshotProvider::buildRuntimeSnapshot()`
- `MQTTModule::publish()`

### Structures manipulées (numérique)

- `IOModule::AnalogSlot`
- `IOAnalogSample`
- `IOEndpointRuntime`
- `DataChangedPayload`
- `RuntimeDispatchModule::RouteEntry` / `RouteSnapshot`

## Flowchart 3: changement d'un état actuateur runtime

Exemple typique: sortie digitale (`rt/io/output/dN`) pilotée par `PoolDeviceModule`.

```mermaid
flowchart TD
A["PoolDeviceModule::tickDevices_() ou cmd"] --> B["PoolDeviceModule::writeIo_(ioId,on)"]
B --> C["ioSvc_->writeDigital(ctx,id,on,ts)"]
C --> D["IOModule::svcWriteDigital_ -> ioWriteDigital_()"]

D --> E["DigitalActuatorEndpoint::write(in)"]
E --> F["setIoEndpointBool(ds, rtIdx, on, ts, DIRTY_ACTUATORS)"]
F --> G["DataStore::notifyChanged(IoBase+rtIdx, DIRTY_ACTUATORS)"]
G --> H["EventBus::post(DataChangedPayload)"]

H --> I["RuntimeDispatchModule::onEvent_ -> markRouteDirty"]
I --> J["RuntimeDispatchModule::loop"]
J --> K["routeClass == ActuatorImmediate => pas de throttle 10s"]
K --> L["buildRuntimeSnapshot(topic rt/io/output/dN)"]
L --> M["mqttSvc.publish -> MQTTModule::publish"]
```

### Fonctions impliquées (actuateur)

- `PoolDeviceModule::writeIo_()`
- `IOModule::svcWriteDigital_()`
- `IOModule::ioWriteDigital_()`
- `setIoEndpointBool()`
- `DataStore::notifyChanged()`
- `RuntimeDispatchModule::onEvent_()`
- `RuntimeDispatchModule::loop()`
- `MQTTModule::publish()`

### Structures manipulées (actuateur)

- `IOModule::DigitalSlot`
- `IOEndpointValue`
- `IOEndpointRuntime`
- `DataChangedPayload`
- `RuntimeDispatchModule::RouteEntry`

## Politique de dispatch runtime (état actuel)

- `NumericThrottled`: publication max 1 fois / 10s par route.
- `ActuatorImmediate`: publication immédiate, sans attendre la fenêtre 10s.
- Retry publication: backoff exponentiel `250ms -> 500ms -> ... -> 5s`.
- Déduplication: si `ts <= lastPublishedTs` (et pas `force`), la route n'est pas republiée.
- Reconnexion MQTT: `DATAKEY_MQTT_READY=true` force un `markAllRoutesDirty(true)`.

## DataKeys et plages runtime

Référence `include/Core/DataKeys.h`:
- clés fixes réseau/temps/mqtt/ha (`1..12` hors trous)
- `40..63`: IO endpoints (`IoBase + idx`)
- `80..87`: pool-device state
- `88..95`: pool-device metrics

## Rôle exact de MQTTModule après refactor

`MQTTModule` ne fait plus la logique de throttling runtime par route.
Il reste responsable de:
- transport MQTT (`publish`, connexion, QoS/outbox guards, low-heap guards)
- commandes `cmd` / `cfg/set`
- publication config (`cfg/*`), alarmes et snapshots périodiques non liés au dispatcher runtime (`rt/network/state`, `rt/system/state`)

Le throttling runtime capteurs/actionneurs est porté par `RuntimeDispatchModule`.
