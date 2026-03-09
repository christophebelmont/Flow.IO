# SystemMonitorModule (`moduleId: sysmon`)

## Rôle

Supervision périodique:
- heap, fragmentation, RSSI/IP WiFi
- stack watermark des tasks modules
- résumé périodique des écritures NVS

Type: module actif.

## Dépendances

- `loghub`

## Affinité / cadence

- core: 0
- task: `sysmon`
- loop: delay 200ms
- traces toujours actives (niveau `Info`)
- période traces pilotée par config (`trace_period_ms`, défaut 5000)

## Services consommés

- `wifi` (`WifiService`)
- `config` (`ConfigStoreService`) [référence disponible]
- `loghub`

## Services exposés

Aucun.

## Config / NVS

Module config: `sysmon` (`moduleId = ConfigModuleId::SystemMonitor`, branche locale `1`)
- `trace_period_ms` (`NvsKeys::SystemMonitor::TracePeriodMs`)

## EventBus / DataStore / MQTT

Aucun direct.

## Particularités

- accepte un pointeur `ModuleManager` pour inspecter les stacks (`setModuleManager`)
- format stack log: `module@cX=watermark` (avec `!` si faible)
- log des écritures NVS via `ConfigStore::logNvsWriteSummaryIfDue()`
