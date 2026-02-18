# Services Core et invocation

Toutes les interfaces sont sous `src/Core/Services/` et agrégées par `src/Core/Services/Services.h`.

## Utilisation standard

Exemple d'accès à un service dans `init()`:

```cpp
void MyModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    const IOServiceV2* io = services.get<IOServiceV2>("io");
    const TimeService* time = services.get<TimeService>("time");
    // vérifier null avant usage
}
```

Exemple d'exposition d'un service:

```cpp
static MyService svc{ /* fn pointers */, this };
services.add("myservice", &svc);
```

## Service IDs utilisés dans le firmware

- `loghub` -> `LogHubService`
- `logsinks` -> `LogSinkRegistryService`
- `eventbus` -> `EventBusService`
- `config` -> `ConfigStoreService`
- `datastore` -> `DataStoreService`
- `cmd` -> `CommandService`
- `hmi` -> `HmiService`
- `alarms` -> `AlarmService`
- `wifi` -> `WifiService`
- `time` -> `TimeService`
- `time.scheduler` -> `TimeSchedulerService`
- `mqtt` -> `MqttService`
- `ha` -> `HAService`
- `io` -> `IOServiceV2`
- `pooldev` -> `PoolDeviceService`

## Contrats principaux

### `CommandService`
- `registerHandler(cmd, fn, userCtx)`
- `execute(cmd, json, args, reply, replyLen)`
- Utilisé par MQTT (`cmd`) et console logique interne.

### `HmiService`
- navigation menu config (`openConfigHome`, `openConfigModule`)
- synchronisation affichage (`requestRefresh`)
- snapshot JSON menu (`buildConfigMenuJson`)

### `ConfigStoreService`
- `applyJson(json)`
- `toJson(out)`
- `toJsonModule(module, out, outLen, truncated)`
- `listModules(out, max)`

### `DataStoreService`
- Expose `DataStore* store`
- accès lecture/écriture via helpers runtime (`WifiRuntime.h`, `IORuntime.h`, etc.)

### `EventBusService`
- Expose `EventBus* bus`
- abonnement via `subscribe(EventId, cb, user)`
- publication via `post(EventId, payload, len)`

### `AlarmService`
- enregistrement d'alarme (`registerAlarm`)
- ack (`ack`, `ackAll`)
- état (`isActive`, `isAcked`, `activeCount`, `highestSeverity`)
- snapshots (`buildSnapshot`, `listIds`, `buildAlarmState`)

### `WifiService`
- état (`state`)
- connectivité (`isConnected`)
- IP string (`getIP`)

### `TimeService`
- état sync (`state`, `isSynced`)
- epoch (`epoch`)
- format local (`formatLocalTime`)

### `TimeSchedulerService`
- gestion slots (`setSlot/getSlot/clearSlot/clearAll`)
- observation (`usedCount/activeMask/isActive`)

### `MqttService`
- publication (`publish(topic,payload,qos,retain)`)
- format topic (`formatTopic(suffix)`)
- état (`isConnected`)

### `HAService`
- enregistrement entités discovery:
  - `addSensor`
  - `addBinarySensor`
  - `addSwitch`
  - `addNumber`
  - `addButton`
- pour `addSensor`, l'entrée peut porter `hasEntityName` pour publier `has_entity_name=true` côté discovery
- refresh: `requestRefresh`

### `IOServiceV2`
- découverte endpoints: `count`, `idAt`, `meta`
- I/O digital: `readDigital`, `writeDigital`
- I/O analog: `readAnalog`
- tick/cycle: `tick`, `lastCycle`

### `PoolDeviceService`
- inventory: `count`, `meta`
- états: `readActualOn`
- commandes: `writeDesired`, `refillTank`

## Bonnes pratiques

- Toujours vérifier la présence du service (`nullptr`) en init.
- Ne pas conserver de pointeurs internes non valides: les services sont stables après init.
- Pour les chemins critiques (loop), éviter parsing JSON ou allocations dynamiques répétées.
