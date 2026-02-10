# SystemModule

## General description

`SystemModule` registers base system commands (ping, reboot, factory reset) on the shared command service.

## Module dependencies

- `moduleId`: `system`
- Declared dependencies: `loghub`, `cmd`

## Provided services

- None.

## Consumed services

- `cmd` (`CommandService`) to register:
  - `system.ping`
  - `system.reboot`
  - `system.factory_reset`
- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- No `ConfigStore` keys.

## DataStore values used

- No `DataStore` read/write.
