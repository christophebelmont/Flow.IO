# ConfigStoreModule

## General description

`ConfigStoreModule` exposes `ConfigStore` operations (JSON patch apply, full/module JSON export, module listing). It is the configuration facade used by MQTT, HA, and other modules.

## Module dependencies

- `moduleId`: `config`
- Declared dependencies: `loghub`

## Provided services

- `config` -> `ConfigStoreService`
  - `applyJson`
  - `toJson`
  - `toJsonModule`
  - `listModules`

## Consumed services

- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- This module exposes the global store; it does not own dedicated keys.

## DataStore values used

- No `DataStore` read/write.
