# Topologie MQTT détaillée

Base topic calculé:
- `<baseTopic>/<deviceId>/...`
- `baseTopic`: config `mqtt.baseTopic` (défaut `flowio`)
- `deviceId`: `ESP32-XXXXXX` (suffix MAC)

## Channels de contrôle

- `cmd`: `<base>/<device>/cmd`
- `ack`: `<base>/<device>/ack`
- `status`: `<base>/<device>/status` (LWT + online)
- `cfg/set`: `<base>/<device>/cfg/set`
- `cfg/ack`: `<base>/<device>/cfg/ack`

### Souscriptions

`MQTTModule` subscribe à:
- `cmd` (QoS 0)
- `cfg/set` (QoS 1)

### Publications état

- `status` retain=true:
  - online: `{"online":true}`
  - LWT offline: `{"online":false}`

## Publication configuration (`cfg/*`)

### Règle générale

Pour chaque module de `ConfigStore::listModules()`:
- topic: `<base>/<device>/cfg/<module>`
- payload: JSON module (`toJsonModule`)

### Cas spécial `time/scheduler`

- root: `<base>/<device>/cfg/time/scheduler`
  - payload: `{"mode":"per_slot","slots":16}`
- détail slot: `<base>/<device>/cfg/time/scheduler/slotN`
  - payload complet du slot (used/event_id/mode/start/end...)

### Déclenchement

- publication initiale/ramp à la connexion MQTT
- publication incrémentale par `ConfigChanged` (branch routing)
- reconnect MQTT si changement de clé de connexion (`host`, `port`, `user`, `pass`, `baseTopic`)

### Exemples de champs de configuration notables

- `cfg/pdm/pdN`:
  - `flow_l_h`
  - `tank_cap_ml`
  - `tank_init_ml`
  - `max_uptime_day_s`
- `cfg/poollogic`:
  - `delay_pids_min`
  - `ph_setpoint`, `orp_setpoint`
  - `ph_window_ms`, `orp_window_ms`
  - `psi_low_th`, `psi_high_th`

## Publication runtime (`rt/*`)

### Multiplex runtime (depuis `main.cpp`)

Routage snapshots providers:
- `rt/io/input/aN`
- `rt/io/input/iN`
- `rt/io/output/dN`
- `rt/pdm/state/pdN`
- `rt/pdm/metrics/pdN`
- `rt/poollogic/ph`
- `rt/poollogic/orp`

Le callback multiplex (`rt/runtime/mux`) publie directement les routes détaillées et ne publie pas de payload sur son topic lié.

### Snapshots périodiques additionnels

- `<base>/<device>/rt/runtime/state` (30s)
- `<base>/<device>/rt/network/state` (60s)
- `<base>/<device>/rt/system/state` (60s)

### Alarmes

- meta: `<base>/<device>/rt/alarms/m`
- par id: `<base>/<device>/rt/alarms/id<alarmId>`

## Débit et throttling

### Pourquoi ce mécanisme existe

Le throttling n'est pas seulement une optimisation réseau.
Il sert à:
- réduire la charge CPU/JSON côté ESP32
- limiter le trafic WiFi et les bursts MQTT
- protéger le broker et le client domotique d'un flux trop fin
- conserver une réactivité élevée sur les actionneurs (priorité sécurité/état réel)

### Paramètre principal

- `mqtt.sens_min_pub_ms` (`sensorMinPublishMs`)
- défaut: `20000 ms`
- effet: période minimale entre deux publications runtime "complètes" capteurs/actionneurs
- cas particulier: `0` désactive la fenêtre de throttling (publish dès qu'il y a du pending)

### Pipeline runtime réel

1. un module runtime écrit dans `DataStore` (`setIoEndpoint*`, `setPoolDeviceRuntime*`, etc.)
2. `DataStore::notifyChanged()` publie `DataSnapshotAvailable` avec `dirtyFlags`
3. `MQTTModule` OR ces flags dans `sensorsPendingDirtyMask` et marque `sensorsPending=true`
4. la boucle MQTT décide quand déclencher le callback mux runtime
5. le callback `publishRuntimeStates()` publie route par route (`rt/io/*`, `rt/pdm/*`)

### Sélection des routes (dirty mask + timestamp)

Le mux runtime applique deux filtres:
- filtre catégorie: une route n'est évaluée que si son `dirtyMask` intersecte `activeSensorsDirtyMask`
- filtre anti-duplicate: si `ts <= lastPublishedTs` la route est ignorée

Conséquence:
- même si `DataSnapshotAvailable` transporte un masque large/cumulatif, les routes inchangées ne sont pas republiées

### Priorité actionneurs pendant la fenêtre throttle

Si la fenêtre `sensorMinPublishMs` est encore active:
- et qu'il y a `DIRTY_ACTUATORS`, MQTT publie immédiatement un passage "actionneurs only"
- les capteurs peuvent attendre la fin de fenêtre

Raison:
- éviter un état HA obsolète sur des sorties critiques (pompes, relais)

### Exceptions startup/reconnect

À la reconnexion MQTT:
- `sensorsPendingDirtyMask` est forcé à `DIRTY_SENSORS | DIRTY_ACTUATORS`
- `lastSensorsPublishMs` est remis à `0`
- publication config `cfg/*` est volontairement différée pour laisser passer d'abord le runtime actionneurs

Cas particulier supplémentaire:
- pendant `StartupActuatorRetryMs` (`3000 ms`), les actionneurs peuvent être republiés plusieurs fois (best effort) pour recoller l'état domotique

Au niveau des routes:
- `rt/io/output/*` et `rt/pdm/state/*` ont aussi un `startupForce` (au moins une publication forcée même hors logique dirty standard)

### Cas particuliers importants

- le callback mux est attaché au topic `rt/runtime/mux` mais ne publie pas de payload sur ce topic; il publie directement les routes détaillées
- si `sensorsPendingDirtyMask` ne contient aucun bit utile, MQTT réarme un masque par défaut `DIRTY_SENSORS | DIRTY_ACTUATORS` (garde-fou)
- les publications runtime périodiques (`rt/runtime/state`, `rt/network/state`, `rt/system/state`) ne dépendent pas de `sensorMinPublishMs`
- les alarmes (`rt/alarms/*`) suivent leur propre file pending/full-sync, indépendamment du throttling runtime capteurs/actionneurs
- `cfg/*` suit une logique dédiée avec ramp au pas fixe (`CfgRampStepMs = 100 ms`) pour lisser la charge
- `cfg/*` est aussi publié en incrémental via `ConfigChanged` (routing par branch)

### Comportement en surcharge et limites

- EventBus est non bloquant (post best effort), donc des événements peuvent être perdus si la queue est saturée
- les `publish()` MQTT sont best effort; un échec n'arrête pas la boucle
- la robustesse globale repose sur les nouveaux événements dirty, les retries startup/reconnect actionneurs et les snapshots périodiques de supervision

## Commandes MQTT (payload type)

### `cmd`

Attendu:
```json
{"cmd":"pooldevice.write","args":{"slot":1,"value":true}}
```
Réponse sur `ack`:
- succès: `{"ok":true,"cmd":"...","reply":{...}}`
- erreur: JSON d'erreur standard (`ErrorCode`)

### `cfg/set`

Attendu (patch config multi-modules):
```json
{"poollogic":{"auto_mode":true},"pdm/pd1":{"flow_l_h":1.4}}
```
Réponse sur `cfg/ack`:
- succès: `{"ok":true,...}`
- échec: JSON d'erreur standard
