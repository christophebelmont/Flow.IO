# DataStoreModule

## General description

`DataStoreModule` instantiates the global runtime `DataStore` and exposes it to the rest of the system.

## Module dependencies

- `moduleId`: `datastore`
- Declared dependencies: `eventbus`

## Provided services

- `datastore` -> `DataStoreService` (pointer to global `DataStore`)

## Consumed services

- `eventbus` (`EventBusService`) to wire notifications (`DataChanged`, `DataSnapshotAvailable`)

## ConfigStore values used

- No `ConfigStore` keys.

## DataStore values used

- Hosts the global `DataStore` instance.
- No module-specific DataKey ownership.
