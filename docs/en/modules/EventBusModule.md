# EventBusModule

## General description

`EventBusModule` owns the central `EventBus` instance, runs dispatch in its task loop, and emits system startup notification.

## Module dependencies

- `moduleId`: `eventbus`
- Declared dependencies: `loghub`

## Provided services

- `eventbus` -> `EventBusService` (access to event bus)

## Consumed services

- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- No `ConfigStore` keys.

## DataStore values used

- No direct `DataStore` read/write.
