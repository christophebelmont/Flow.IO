# CommandModule

## General description

`CommandModule` centralizes command handler registration and execution (`system.*`, `io.write`, `time.*`, etc.) through a common registry.

## Module dependencies

- `moduleId`: `cmd`
- Declared dependencies: `loghub`

## Provided services

- `cmd` -> `CommandService`
  - `registerHandler`
  - `execute`

## Consumed services

- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- No `ConfigStore` keys.

## DataStore values used

- No `DataStore` read/write.
