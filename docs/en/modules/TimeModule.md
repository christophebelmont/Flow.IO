# TimeModule

## General description

`TimeModule` synchronizes system time (NTP), maintains a 16-slot scheduler for one-shot/recurring events, and publishes scheduling events for domain modules.

## Module dependencies

- `moduleId`: `time`
- Declared dependencies: `loghub`, `datastore`, `cmd`, `eventbus`

## Provided services

- `time` -> `TimeService`
- `time.scheduler` -> `TimeSchedulerService`

## Consumed services

- `eventbus` (`EventBusService`) to:
  - consume `DataChanged` and `ConfigChanged`
  - publish `SchedulerEventTriggered`
- `cmd` (`CommandService`) to register:
  - `time.resync`, `ntp.resync`
  - `time.scheduler.info|get|set|clear|clear_all`
- `datastore` (`DataStoreService`) to publish time sync state
- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- JSON module: `time`
  - `server1`
  - `server2`
  - `tz`
  - `enabled`
  - `week_start_monday`
- JSON module: `time/scheduler`
  - `slots_blob`

## DataStore values used

- Writes:
  - `time.timeReady` (DataKey `DATAKEY_TIME_READY` = 3, dirty `DIRTY_TIME`)
- Reads:
  - `wifi.ready` to gate synchronization attempts
