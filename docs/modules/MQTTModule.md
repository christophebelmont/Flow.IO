# MQTTModule (`moduleId: mqtt`)

## Rôle

Cœur MQTT unifié (transport + scheduling TX):
- connexion broker, souscriptions RX, reconnexion/backoff
- chemin TX unique pour **toutes** les publications MQTT
- files de jobs par priorité (`High`, `Normal`, `Low`)
- build topic/payload **à la demande** dans le buffer central
- dédup/coalescing par clé logique `(producerId, messageId)`
- exposition du service `MqttService`

Type: module actif.

## Dépendances

- `loghub`
- `wifi`
- `cmd`
- `time`
- `alarms`

## Affinité / cadence

- core: `0`
- task: `mqtt`
- stack: `Limits::Mqtt::TaskStackSize` (`5712`)
- loop delay: `Limits::Mqtt::Timing::LoopDelayMs` (`50 ms`)

## Services exposés

- `mqtt` -> `MqttService`
  - `enqueue(ctx, producerId, messageId, prio, flags)`
  - `registerProducer(ctx, producer)`
  - `formatTopic(ctx, suffix, out, outLen)`
  - `isConnected(ctx)`

Contrat producteur (`MqttPublishProducer`):
- `buildMessage(ctx, messageId, buildCtx)`
- `onMessagePublished(...)`
- `onMessageDeferred(...)`
- `onMessageDropped(...)`
- `onTransportTick(...)` (optionnel)

## Services consommés

- `wifi` (`WifiService`)
- `cmd` (`CommandService`)
- `config` (`ConfigStoreService`)
- `time.scheduler` (`TimeSchedulerService`)
- `alarms` (`AlarmService`)
- `eventbus` (`EventBusService`)
- `datastore` (`DataStoreService`)

## Config / NVS

Module config: `mqtt`
- `host`
- `port`
- `user`
- `pass`
- `baseTopic`
- `topicDeviceId` (optionnel, segment `<deviceId>` des topics)
- `enabled`

Identité config interne:
- `moduleId = ConfigModuleId::Mqtt`
- `localBranchId = 1` (route cfg MQTT locale)

## RX MQTT

Topics souscrits:
- `<base>/<device>/cmd` (QoS 0)
- `<base>/<device>/cfg/set` (QoS 1)

Pipeline RX:
1. callback IDF `MQTT_EVENT_DATA`
2. copie vers queue RX bornée (`RxMsg`)
3. traitement dans la task `mqtt`
4. ACK/erreur sur `ack` ou `cfg/ack`

Queue RX:
- longueur: `Limits::Mqtt::Capacity::RxQueueLen` (`8`)
- création: `xQueueCreateStatic(...)`
- stockage: heap alloué une fois à l'init (`heap_caps_malloc`)

## TX unifié (jobs)

### Structure logique

Un job ne contient pas de topic/payload persistants:
- `producerId`
- `messageId`
- `priority`
- `flags`
- `retryCount`
- `notBeforeMs`

Capacités:
- slots jobs globaux: `MaxJobs = 80`
- ring `High`: `80`
- ring `Normal`: `80`
- ring `Low`: `60`

Politique:
- ordonnanceur priorité `High -> Normal -> Low`
- budget de traitement: `ProcessBudgetPerTick = 8`
- retry centralisé: backoff exponentiel `250 ms -> 10 s`
- coalescing: un seul job vivant par `(producerId,messageId)`

### Producteurs enregistrés (principaux)

- ACK (`producerId=1`)
- Status (`producerId=2`)
- Runtime (`producerId=4`, via `RuntimeProducer`)
- Alarm (`producerId=5`)
- Config routes MQTT internes (`producerId=41`)
- autres modules autoportés (HA, IO, PoolDevice, PoolLogic, Wifi, Time, etc.) via `MqttConfigRouteProducer`

## Topics principaux

- `<base>/<device>/status` (retain, LWT online/offline)
- `<base>/<device>/ack`
- `<base>/<device>/cfg/ack`
- `<base>/<device>/cfg/*` (publiés par producteurs config module-owned)
- `<base>/<device>/rt/*` (runtime producer + publishers périodiques)

Référence détaillée: `../core/mqtt-topics.md`.

Note:
- si `mqtt.topicDeviceId` est vide, l'ID topic est auto-généré depuis la MAC (`ESP32-XXXXXX`)
- si renseigné, il est utilisé pour `<base>/<deviceId>/...` et le `client_id` MQTT

## EventBus

Abonnements:
- `DataChanged`
- `ConfigChanged`
- `AlarmRaised`, `AlarmCleared`, `AlarmAcked`, `AlarmSilenceChanged`, `AlarmConditionChanged`

Effets:
- `DataChanged(DATAKEY_WIFI_READY)`: gating connexion MQTT
- `ConfigChanged` sur clés de connexion MQTT: reconnect propre
- événements alarmes: enqueue des jobs `rt/alarms/*`

## DataStore

Écritures runtime:
- `MqttReady`
- compteurs RX: `rxDrop`, `oversizeDrop`, `parseFail`, `handlerFail`

## Signification des logs MQTT

### Occupation queues/jobs

`queue occ max/boot jobs=A/80 qh=B/80 qn=C/80 ql=D/60`
- niveau log: `DEBUG` (métrologie uniquement, pas d'alerte à elle seule)
- max observés depuis le boot (non remis à zéro)
- `jobs`: slots jobs utilisés
- `qh/qn/ql`: profondeur max des rings High/Normal/Low

### Rejet enqueue (noyau)

`enqueue reject reason=<slot_full|queue_full> producer=P msg=M prio=R jobs=...`
- `slot_full`: plus de slot job libre (`80/80`)
- `queue_full`: ring de priorité cible saturé
- peut être silencé par flag `MqttEnqueueFlags::SilentRejectLog`

### Métriques cfg route producer

`cfgq p=.. e=.. rf=.. rt=.. ok=.. to=.. tf=.. tt=.. tk=.. tto=..`
- `p`: routes cfg pending (global)
- `e`: routes en attente de ré-enqueue transport
- `rf`: refus enqueue fenêtre 5s
- `rt`: tentatives retry fenêtre 5s
- `ok`: retry acceptés fenêtre 5s
- `to`: timeouts fenêtre 5s (route perdue après `10 s` de refus)
- `tf/tt/tk/tto`: cumuls boot (`refus/tries/ok/timeouts`)
- niveau log: `DEBUG` par défaut, `WARN` si `tto > 0` (au moins un timeout cumulé depuis le boot)

### Logs IDF MQTT client

Exemple:
`MQTT_CLIENT: mqtt_message_receive: transport_read() error: errno=128`
- erreur transport bas niveau TCP/TLS côté client IDF
- déclenche en pratique une déconnexion puis le cycle de reconnexion/backoff du module
