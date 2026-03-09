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
  - `buildSnapshot`, `listIds`, `buildAlarmState`, `buildPacked`

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
- `alarms.ack_slot` (args `{slot}` -> résolution `slot -> id`)
- `alarms.ack_all`

## Modèle d'état d'une alarme

Chaque slot garde notamment:
- `lastCond`: `False` / `True` / `Unknown`
- `active`: état d'alarme actif
- `acked`: acquittement utilisateur (utile si `latched=true`)
- timers `onSinceMs` / `offSinceMs`

Transitions:
- `cond=True` pendant `onDelayMs` -> `active=true`
- `cond=False`:
  - si non latched -> clear après `offDelayMs`
  - si latched et non acked -> reste active
  - si latched et acked -> clear après `offDelayMs`
- `cond=Unknown` -> pas de transition (timers annulés, état conservé)

## Sémantique latch / ack

- `latched=false`: clear automatique quand la condition retombe
- `latched=true`: clear nécessite un acquittement (`ack`) puis condition false
- `ack` ne force pas un clear immédiat si `offDelayMs > 0`
- `ack_all` ne touche que les alarmes latched actives non encore ackées

Conséquence opérationnelle importante:
- une alarme latched peut rester `active=true` même si la condition est redevenue fausse (tant qu'elle n'est pas ackée)
- c'est voulu pour garder la trace d'un défaut de sécurité jusqu'à intervention

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

## Notifications et anti-spam

Le module applique un `minRepeatMs` par alarme pour limiter la fréquence des notifications EventBus
quand une condition reste active/oscille rapidement.

## Alarmes définies actuellement

Le module est générique. Dans le projet actuel, `PoolLogicModule` enregistre:
- `AlarmId::PoolPsiLow`
- `AlarmId::PoolPsiHigh`
- `AlarmId::PoolPhTankLow`
- `AlarmId::PoolChlorineTankLow`

Pour `PoolLogic`, ces alarmes servent d'interlock sécurité:
- `PoolLogic` lit `isActive()` sur ces IDs
- si l'une est active, la filtration est forcée OFF (auto et manuel)
