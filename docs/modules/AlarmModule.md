# AlarmModule (`moduleId: alarms`)

## Rôle

Moteur d'alarmes central:
- enregistrement des définitions d'alarmes (`AlarmRegistration`)
- évaluation cyclique de conditions (`AlarmCondFn`)
- latching, ack, délais ON/OFF, snapshots
- émission d'événements de cycle de vie d'alarme

Type: module actif.

## Dépendances

- `loghub`
- `eventbus`
- `cmd`

## Affinité / cadence

- core: 1
- task: `alarms`
- période d'évaluation: config `eval_period_ms` clampée [25..5000] ms

## Services exposés

- `alarms` -> `AlarmService`
  - `registerAlarm`, `ack`, `ackAll`
  - `isActive`, `isAcked`, `activeCount`, `highestSeverity`
  - `buildSnapshot`, `listIds`, `buildAlarmState`

## Services consommés

- `eventbus` (émission événements)
- `cmd` (commandes)

## Config / NVS

Module config `alarms` (`moduleId = ConfigModuleId::Alarms`, branche locale `1`):
- `enabled`
- `eval_period_ms`

## Commandes

- `alarms.list`
- `alarms.ack` (args `{id}`)
- `alarms.ack_all`

## EventBus

Émissions:
- `AlarmConditionChanged`
- `AlarmRaised`
- `AlarmCleared`
- `AlarmAcked`

Abonnements:
- aucun

## DataStore / MQTT

Aucun accès direct au DataStore.
MQTT consomme les événements alarmes via `MQTTModule`.

## Alarmes définies actuellement

Le module est générique. Dans le projet actuel, `PoolLogicModule` enregistre:
- `AlarmId::PoolPsiLow`
- `AlarmId::PoolPsiHigh`
