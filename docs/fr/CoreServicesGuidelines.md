# Core Services Guidelines

This guide defines when to create a Core service in `src/Core/Services/` and when to prefer `EventBus` or `DataStore`.

## 1) Quick Decision Rule

- Use a **Core service** for a **callable capability** (synchronous API) shared by modules.
- Use **EventBus** for **asynchronous notifications** (fire-and-forget).
- Use **DataStore** for **shared runtime state** (latest known values + dirty flags).

## 2) When to Create a Core Service

Create a service only if all conditions are true:

1. There is a clear owner (single producer module).
2. Consumers need direct calls, not only notifications.
3. The contract is stable and reusable.
4. The need is not just runtime state better represented in `DataStore`.

## 3) When NOT to Create a Core Service

Do not create a service if:

1. The need is asynchronous fan-out (`EventBus`).
2. The need is reading current state (`DataStore`).
3. The contract is local and short-lived.
4. An equivalent service already exists.

## 4) Current Core Service Inventory in Flow.IO

- `eventbus` -> `EventBusService` (owner: `EventBusModule`)
- `config` -> `ConfigStoreService` (owner: `ConfigStoreModule`)
- `datastore` -> `DataStoreService` (owner: `DataStoreModule`)
- `cmd` -> `CommandService` (owner: `CommandModule`)
- `wifi` -> `WifiService` (owner: `WifiModule`)
- `mqtt` -> `MqttService` (owner: `MQTTModule`)
- `time` -> `TimeService` (owner: `TimeModule`)
- `time.scheduler` -> `TimeSchedulerService` (owner: `TimeModule`)
- `io_leds` -> `IOLedMaskService` (owner: `IOModule`, only when PCF8574 is enabled)
- `loghub` -> `LogHubService` (owner: `LogHubModule`)
- `logsinks` -> `LogSinkRegistryService` (owner: `LogHubModule`)

## 5) Implementation Rules

- Add the service interface in `src/Core/Services/`.
- Add it to `src/Core/Services/Services.h`.
- Register it in the producer with `services.add("service-id", &svc)`.
- Declare consumer dependencies in `dependency()`.
- On consumer side, always validate pointers returned by `services.get<...>()`.

## 6) Service vs DataStore Boundary in Flow.IO

- Action commands (`io.write`, `pool.write`) remain command-level APIs; resulting state is published to `DataStore`.
- Runtime state (`wifi.ready`, `mqtt.ready`, `time.ready`, `io.*`, `pool.*`) belongs in `DataStore`.
- Asynchronous orchestration (for example scheduler-driven pool counter resets) uses `EventBus`.

## 7) Checklist Before Adding a New Service

1. Who is the single owner of this capability?
2. Why is a direct call required?
3. Why is `EventBus` not sufficient?
4. Why is `DataStore` not sufficient?
5. Which modules actually consume it today?
6. What stable `service-id` will be used?

If two or more answers are weak, prefer `EventBus` and/or `DataStore`.
