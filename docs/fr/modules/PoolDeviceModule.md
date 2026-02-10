# PoolDeviceModule

## General description

`PoolDeviceModule` is the pool equipment domain layer. It applies interlock rules, exposes `pool.write` / `pool.refill`, tracks run counters, and computes injected volume/tank state.

## Module dependencies

- `moduleId`: `pooldev`
- Declared dependencies: `loghub`, `datastore`, `cmd`, `time`, `io`, `mqtt`, `eventbus`
- Note: core behavior mainly consumes `cmd`, `datastore`, and `eventbus`; other dependencies enforce a coherent startup order.

## Provided services

- None.

## Consumed services

- `cmd` (`CommandService`) to:
  - register `pool.write`
  - register `pool.refill`
  - execute `io.write` internally
- `eventbus` (`EventBusService`) to consume `SchedulerEventTriggered` (day/week/month resets)
- `datastore` (`DataStoreService`) to read IO output states and publish pool runtime
- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- Dynamic JSON modules: `pdm/pdN` (N for each declared device, up to 8)
- Keys per device:
  - `enabled`
  - `type`
  - `depends_on_mask`
  - `flow_l_h`
  - `tank_capacity_ml`
  - `tank_initial_ml`

## DataStore values used

- Writes:
  - `pool.devices[idx]` via `setPoolDeviceRuntime`
  - DataKey range: `DATAKEY_POOL_DEVICE_BASE + idx` (`DATAKEY_POOL_DEVICE_BASE = 80`)
  - Dirty flag: `DIRTY_SENSORS`
- Reads:
  - `io.endpoints[idx]` to evaluate actual state of mapped digital outputs
