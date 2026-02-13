# Flow.IO Module Quality Gates

This document defines the mandatory quality gates for Flow.IO modules on ESP32-class embedded targets.

Scope:
- all active and passive modules under `src/Modules`
- core stores/services when they expose behavior used by modules (`ConfigStore`, `DataStore`, service interfaces)

Audience:
- firmware developers
- reviewers validating merges
- maintainers running release readiness checks

## Scoring Model

Each module is scored from `0.0` to `10.0`.

Weights:
- `G1` Memory discipline: 20%
- `G2` Service-only coupling: 15%
- `G3` Static topology and bounded containers: 10%
- `G4` Bounded JSON handling (no heap): 10%
- `G5` No magic numbers in logic paths: 10%
- `G6` Error handling and observability: 10%
- `G7` Deterministic timing and scheduler safety: 10%
- `G8` Config and persistence hygiene: 5%
- `G9` Testability and simulation hooks: 5%
- `G10` API and documentation consistency: 5%

Interpretation:
- `>= 9.0`: release-grade
- `8.0 - 8.9`: good, minor debt
- `7.0 - 7.9`: acceptable for dev branch, not ideal for production freeze
- `< 7.0`: must be remediated before release

---

## Gate G1 - Memory Discipline (No Runtime Heap Drift)

Rule:
- No unbounded or repeated heap usage in runtime loops.
- `new` is allowed only during one-time startup provisioning and must remain stable for process lifetime.
- `String`, `std::vector`, dynamic STL containers are forbidden in module runtime paths.

Implementation:
- Allocate fixed objects in module members or fixed arrays.
- If external libraries require heap, create once in `init()/configureRuntime_()` and never churn.
- Do not `delete` and recreate objects based on transient runtime conditions.
- Use bounded stack buffers for formatting and temporary payloads.

Acceptance checks:
- `rg -n "String\\b|std::vector|malloc\\(|realloc\\(|free\\(" src/Modules src/Core`
- inspect every `new` for one-time startup-only behavior

---

## Gate G2 - Service-Only Coupling

Rule:
- Module-to-module communication must go through typed service interfaces.
- No direct calls into another module class implementation.

Implementation:
- declare dependencies in `dependencyCount()/dependency()`
- acquire interfaces via `services.get<T>("service.id")`
- expose capabilities with `services.add("service.id", &svcStruct)`
- pass numeric IDs (`IoId`, slot IDs) through service APIs, not textual lookup flows

Acceptance checks:
- no include of another module `.cpp`
- cross-module behavior visible via `I*.h` service contracts only

---

## Gate G3 - Static Topology and Bounded Containers

Rule:
- All cardinalities must be finite and explicit (`MAX_*`, `*_COUNT`, fixed arrays).
- No growth-by-input behavior at runtime.

Implementation:
- define maxima in compile-time constants
- reject registrations when capacity is reached
- use static route tables / registries with strict bounds

Acceptance checks:
- every registry/list has a max and overflow handling path
- loops over arrays are bounded by compile-time constants

---

## Gate G4 - Bounded JSON Handling (No Heap)

Rule:
- JSON parsing and serialization must use bounded documents (`StaticJsonDocument`) or bounded manual serialization.
- Ad-hoc string parsing (`strstr`/`atoi`/raw scanning for JSON syntax) is not allowed for command/config JSON payloads.

Implementation:
- use `ArduinoJson` with explicit static capacities per endpoint path
- keep parse error handling explicit (`missing_args`, `invalid_*`, etc.)
- reuse static documents where lifetime and concurrency are safe

Acceptance checks:
- `rg -n "deserializeJson|StaticJsonDocument" src`
- no legacy JSON scanners in module command paths

---

## Gate G5 - No Magic Numbers in Logic Paths

Rule:
- Domain identifiers and mappings (sensor IDs, device slots, endpoint aliases) must be centralized in mapping headers.
- Remaining numeric constants must be algorithmic thresholds or protocol constants with explicit names.

Implementation:
- centralize actuator topology in `Core/Layout/PoolIoMap.h`
- centralize sensor topology in `Core/Layout/PoolSensorMap.h`
- in modules, consume mapping constants instead of repeating raw values

Acceptance checks:
- no duplicated raw sensor/device IDs in multiple modules
- slot-to-meaning mapping exists in one place

---

## Gate G6 - Error Handling and Observability

Rule:
- Every external operation (I/O read/write, scheduler set, network publish) must produce deterministic error handling.
- Failures must be visible via return codes and logs.

Implementation:
- return typed status codes (`IO_OK`, `POOLDEV_SVC_ERR_*`, etc.)
- emit concise logs with context (`slot`, `ioId`, `reason`)
- avoid silent fallback behavior for invalid command/config payloads

Acceptance checks:
- no ignored critical return values in control paths
- warnings/errors include enough context to diagnose field issues

---

## Gate G7 - Deterministic Timing and Scheduler Safety

Rule:
- Control loops, polling, and periodic jobs must be deterministic and bounded.
- No blocking network/storage operation in high-frequency control paths.

Implementation:
- isolate periodic work in scheduler jobs with minimum/maximum periods
- maintain monotonic timestamps and change-sequence semantics
- guard shared flags with critical sections where needed

Acceptance checks:
- module loops include bounded delays
- scheduler jobs have explicit periods and callbacks

---

## Gate G8 - Config and Persistence Hygiene

Rule:
- Runtime defaults and persistent config must be explicit, typed, and discoverable.
- Config application must update only known keys and notify changes.

Implementation:
- register all config variables with `ConfigStore`
- keep NVS key naming stable and bounded
- apply JSON patches via typed conversion and bounded parser

Acceptance checks:
- module config variables are fully declared and registered
- persistence writes happen only on actual value changes

---

## Gate G9 - Testability and Simulation Hooks

Rule:
- Module logic must be testable without real hardware where feasible.
- Hardware details should be isolated behind drivers/services.

Implementation:
- use `IOServiceV2`, `PoolDeviceService`, `TimeSchedulerService` in business modules
- keep logic methods pure where possible (calculation vs side effects)
- provide callback-based or interface-based seams for test doubles

Acceptance checks:
- business modules can run from mocked services
- no hard dependency on concrete hardware classes in high-level logic modules

---

## Gate G10 - API and Documentation Consistency

Rule:
- Public service contracts and module behavior must be documented and kept in sync with code.
- Naming must remain coherent across code, MQTT, and Home Assistant discovery payloads.

Implementation:
- update module docs on contract changes
- keep topic suffixes, object IDs, and IDs deterministic
- document mapping files and ownership boundaries

Acceptance checks:
- docs reference current service names and key topic shapes
- no stale public contract in README/docs

---

## Current Scoreboard (Snapshot)

Snapshot date: `2026-02-13`  
Context: static code review after IOServiceV2 migration and bounded ArduinoJson migration.

| Module | Score (/10) | Main Strengths | Main Gaps |
|---|---:|---|---|
| `CommandModule` | 9.1 | clean service boundary, simple deterministic behavior | limited test artifacts |
| `EventBusModule` | 9.2 | strong decoupling backbone | limited runtime diagnostics depth |
| `IOModule` | 7.2 | strong abstraction model, bounded topology, IOServiceV2 | runtime heap allocations still present in provisioning/drivers |
| `LogHubModule` | 8.9 | clear service contracts for sink registry | moderate testability depth |
| `LogDispatcherModule` | 8.9 | deterministic dispatch path | some coupling to sink service assumptions |
| `LogSerialSinkModule` | 8.9 | bounded formatting path | minor resilience/doc gaps |
| `WifiModule` | 9.0 | clear service contract, robust runtime state publishing | low simulation coverage |
| `TimeModule` | 8.7 | scheduler service clarity, bounded JSON command parsing | some hardcoded reserved-slot semantics |
| `MQTTModule` | 8.7 | good service orchestration, bounded JSON parsing | complexity concentration in `processRx`/publication logic |
| `HAModule` | 8.4 | deterministic discovery payload model, service abstraction | payload-building complexity and limited schema validation |
| `PoolDeviceModule` | 8.9 | strong use of `IOServiceV2` and `PoolDeviceService`, bounded JSON command parsing | remaining hardware/runtime behavior could be more unit-isolated |
| `PoolLogicModule` | 7.8 | business logic decoupled through services | default scheduler/device slots still hardcoded; some rule constants not centralized |
| `ConfigStoreModule` | 8.5 | typed config service bridge | core dependency quality tied to ConfigStore internals |
| `DataStoreModule` | 9.1 | stable core-state service, clean integration | minor diagnostics/test breadth |
| `SystemModule` | 9.0 | straightforward command-oriented module | limited advanced observability |
| `SystemMonitorModule` | 9.0 | useful runtime visibility and service-based coupling | simulation/test hooks can be improved |

---

## Immediate Priorities to Raise Global Score

1. Remove runtime heap from `IOModule` and `OneWireBus` by switching to static object pools or static members.
2. Move remaining `PoolLogicModule` slot defaults to centralized mapping/constants.
3. Continue expanding module-level simulation tests around service contracts for `IOModule`, `PoolLogicModule`, and `HAModule`.
