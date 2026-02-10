# LogHubModule

## General description

`LogHubModule` centralizes application log ingestion and maintains the log sink registry. It is the foundation of the logging pipeline.

## Module dependencies

- `moduleId`: `loghub`
- Declared dependencies: none

## Provided services

- `loghub` -> `LogHubService` (asynchronous enqueue of `LogEntry`)
- `logsinks` -> `LogSinkRegistryService` (register/list sinks)

## Consumed services

- None.

## ConfigStore values used

- No `ConfigStore` keys.

## DataStore values used

- No `DataStore` read/write.
