# TimeModule - Time Sync and Scheduler

This document describes the `time` module (formerly `NTPModule`) and its integrated scheduler.

## 1) Module Role

`TimeModule` provides two capabilities:

- system clock synchronization (current backend: NTP)
- a static scheduler (`16` slots) for one-shot and recurring events/windows

The public contract is backend-agnostic so future time sources (RTC/external) can be added without changing API consumers.

## 2) Exposed Core Services

### 2.1 `time`

Interface: `src/Core/Services/ITime.h`

- `state(ctx)` -> `TimeSyncState`
- `isSynced(ctx)` -> bool
- `epoch(ctx)` -> epoch seconds
- `formatLocalTime(ctx, out, len)` -> `YYYY-MM-DD HH:MM:SS`

### 2.2 `time.scheduler`

Interface: `src/Core/Services/ITimeScheduler.h`

- `setSlot(ctx, slotDef)`
- `getSlot(ctx, slot, outDef)`
- `clearSlot(ctx, slot)`
- `clearAll(ctx)`
- `usedCount(ctx)`
- `activeMask(ctx)`
- `isActive(ctx, slot)`

## 3) Scheduler Model

### 3.1 Capacity and Types

- fixed capacity: `TIME_SCHED_MAX_SLOTS = 16`
- each slot can be:
  - event (`hasEnd=false`)
  - time window (`hasEnd=true`, start/stop)

`activeMask` exposes currently active window slots.

### 3.2 Slot Fields

`TimeSchedulerSlot` includes:

- `slot` (0..15)
- `eventId`
- `enabled`
- `hasEnd`
- `replayStartOnBoot`
- `label`
- `mode` (`RecurringClock` or `OneShotEpoch`)
- recurring fields (`weekdayMask`, start/end hour/minute)
- one-shot fields (`startEpochSec`, `endEpochSec`)

### 3.3 Reserved System Slots

First 3 slots are reserved and auto-rebuilt:

- `0` -> `TIME_EVENT_SYS_DAY_START`
- `1` -> `TIME_EVENT_SYS_WEEK_START`
- `2` -> `TIME_EVENT_SYS_MONTH_START`

Properties:

- not editable via `setSlot`/`clearSlot`
- preserved by `clearAll`
- month event triggers at 00:00 only when `tm_mday == 1`

## 4) ConfigStore Keys

Module JSON: `time`

- `server1`
- `server2`
- `tz`
- `enabled`
- `week_start_monday`

Module JSON: `time/scheduler`

- `slots_blob` (persisted compact scheduler blob)

Legacy `ntp_*` NVS keys are kept for backward compatibility.

## 5) EventBus Contract

Published event: `EventId::SchedulerEventTriggered`

Payload: `SchedulerEventTriggeredPayload`

- `slot`
- `edge` (`Trigger`, `Start`, `Stop`)
- `replayed`
- `eventId`
- `epochSec`
- `activeMask`

## 6) Domain Integration Pattern

The binding between scheduler and domain modules is `eventId`-based (not label-based):

1. domain module reserves event IDs
2. module configures slots through `time.scheduler`
3. module subscribes to `SchedulerEventTriggered`
4. module filters by `eventId` and `edge`

## 7) PoolDevice Integration

`PoolDeviceModule` listens to `SchedulerEventTriggered` and uses reserved system events:

- day start -> reset day counters
- week start -> reset week counters
- month start -> reset month counters

Resets are deferred via a pending mask and applied in module loop for thread safety.

## 8) Debug Commands

Registered commands:

- `time.resync`
- `time.scheduler.info`
- `time.scheduler.get`
- `time.scheduler.set`
- `time.scheduler.clear`
- `time.scheduler.clear_all`

## 9) Accelerated Clock Test Mode

In `src/Modules/Network/TimeModule/TimeModule.cpp`, enable:

```cpp
// #define TIME_TEST_FAST_CLOCK
```

Behavior:

- simulated start: `2020-01-01 00:00:00`
- `5` real minutes = `1` simulated month

Useful for fast validation of day/week/month transitions and scheduler behavior.

## 10) Current Limits

- no alternate backend implementation yet (RTC/external)
- no large catch-up replay strategy after long offline periods
- scheduler persistence is compact blob format, not human-readable JSON
