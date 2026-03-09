# TimeModule (`moduleId: time`)

## Rôle

Synchronisation temps + moteur scheduler interne:
- sync NTP (TZ configurable)
- exposition `TimeService`
- exposition `TimeSchedulerService` (16 slots)
- publication d'événements scheduler sur EventBus

Type: module actif.

## Dépendances

- `loghub`
- `datastore`
- `cmd`
- `eventbus`

## Affinité / cadence

- core: 1
- task: `time`
- loop: toutes les 250ms

## Services exposés

- `time` -> `TimeService`
- `time.scheduler` -> `TimeSchedulerService`

## Services consommés

- `eventbus`
- `datastore`
- `cmd`

## Config / NVS

Branches:
- `moduleId = ConfigModuleId::Time`, branche locale `1` (`module: time`)
  - `server1`, `server2`, `tz`, `enabled`, `week_start_mon`
- `moduleId = ConfigModuleId::Time`, branche locale `2` (`module: time/scheduler`)
  - `slots_blob` (`tm_sched`)

## Commandes

- `time.resync` (alias: `ntp.resync`)
- `time.scheduler.info`
- `time.scheduler.get`
- `time.scheduler.set`
- `time.scheduler.clear`
- `time.scheduler.clear_all`

## EventBus

Abonnements:
- `DataChanged` (clé `WifiReady`)
- `ConfigChanged` (branches locales `1` et `2` du module `Time`)

Publications:
- `SchedulerEventTriggered` avec payload `SchedulerEventTriggeredPayload`

Slots système réservés (0..2):
- `TIME_SLOT_SYS_DAY_START` -> `TIME_EVENT_SYS_DAY_START`
- `TIME_SLOT_SYS_WEEK_START` -> `TIME_EVENT_SYS_WEEK_START`
- `TIME_SLOT_SYS_MONTH_START` -> `TIME_EVENT_SYS_MONTH_START`

## DataStore

Écriture:
- `setTimeReady(...)` -> `DataKeys::TimeReady`

## Persistance scheduler

- serialisation compacte dans `slots_blob`
- recharge en `onConfigLoaded()`
- validation stricte des slots (mode, bornes horaires/epoch)
- slots système toujours ré-appliqués et protégés

## Notes d'intégration

- Les modules métier (ex: `PoolLogicModule`) doivent utiliser `time.scheduler` pour programmer leurs fenêtres.
- Les modules consommateurs doivent écouter `EventId::SchedulerEventTriggered`.
