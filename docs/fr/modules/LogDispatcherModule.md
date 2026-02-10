# LogDispatcherModule

## General description

`LogDispatcherModule` dequeues entries from the log hub and forwards them to all registered sinks in a dedicated task.

## Module dependencies

- `moduleId`: `log.dispatcher`
- Declared dependencies: `loghub`

## Provided services

- None.

## Consumed services

- `loghub` (`LogHubService`) to consume queued log entries
- `logsinks` (`LogSinkRegistryService`) to iterate and write to sinks

## ConfigStore values used

- No `ConfigStore` keys.

## DataStore values used

- No `DataStore` read/write.
