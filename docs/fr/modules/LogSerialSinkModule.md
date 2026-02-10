# LogSerialSinkModule

## General description

`LogSerialSinkModule` registers a serial sink with per-level colors and timestamp formatting (local time when valid, uptime fallback otherwise).

## Module dependencies

- `moduleId`: `log.sink.serial`
- Declared dependencies: `loghub`

## Provided services

- None.

## Consumed services

- `logsinks` (`LogSinkRegistryService`) to add the serial sink

## ConfigStore values used

- No `ConfigStore` keys.

## DataStore values used

- No `DataStore` read/write.
