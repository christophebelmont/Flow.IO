# IOModule - Architecture and Configuration

This document describes the current `io` module implementation.

It replaces the old Sensors/Actuators split with a unified architecture:

- buses (`I2C`, `OneWire`)
- drivers (`ADS1115`, `DS18B20`, `PCF8574`, GPIO)
- endpoints (`analog input`, `digital actuator`, `mask endpoint`)
- registry + scheduler

## 1) Global Behavior

- Single runtime module: `IOModule` (`moduleId = "io"`).
- Asynchronous ADS polling via scheduler (`ads_poll_ms`, default `125 ms`).
- Asynchronous DS18B20 polling via scheduler (`ds_poll_ms`, default `2000 ms`).
- Analog endpoint pipeline:
  1. driver read
  2. `min/max` validation
  3. `RunningMedian(11)` + central average (`5`)
  4. calibration `y = c0*x + c1`
  5. precision rounding
  6. domain callback (`onValueChanged`) defined in `main.cpp`

Important: `io` does not contain business semantics such as pH/ORP/PSI logic.

## 2) ConfigStore Layout

- global keys in JSON module `io`
- analog endpoint modules `io/input/a0` ... `io/input/a4`
- digital output modules `io/output/d0` ... `io/output/d7`

### 2.1 Global Parameters

| JSON key | Type | Default | Description |
|---|---:|---:|---|
| `enabled` | bool | `true` | Enable/disable IO module |
| `i2c_sda` | int | `21` | SDA pin |
| `i2c_scl` | int | `22` | SCL pin |
| `ads_poll_ms` | int | `125` | ADS scheduler period |
| `ds_poll_ms` | int | `2000` | DS18B20 scheduler period |
| `ads_internal_addr` | uint8 | `0x48` | Internal ADS address |
| `ads_external_addr` | uint8 | `0x49` | External ADS address |
| `ads_gain` | int | `0` | ADS1115 PGA code |
| `ads_rate` | int | `1` | ADS data rate (0..7) |
| `pcf_enabled` | bool | `true` | Enable PCF8574 driver |
| `pcf_address` | uint8 | `0x20` | PCF8574 I2C address |
| `pcf_mask_default` | uint8 | `0` | Initial mask at boot |
| `pcf_active_low` | bool | `true` | PCF polarity (`true` = ON at low level) |

### 2.2 `ads_gain` Mapping

`ads_gain` is a code (not a multiplier):

| `ads_gain` | ADS macro | Full scale | LSB (ADS1115) |
|---:|---|---|---|
| `0` | `ADS1X15_GAIN_6144MV` | +/-6.144 V | 0.1875 mV/bit |
| `1` | `ADS1X15_GAIN_4096MV` | +/-4.096 V | 0.1250 mV/bit |
| `2` | `ADS1X15_GAIN_2048MV` | +/-2.048 V | 0.0625 mV/bit |
| `4` | `ADS1X15_GAIN_1024MV` | +/-1.024 V | 0.03125 mV/bit |
| `8` | `ADS1X15_GAIN_0512MV` | +/-0.512 V | 0.015625 mV/bit |
| `16` | `ADS1X15_GAIN_0256MV` | +/-0.256 V | 0.0078125 mV/bit |

### 2.3 Analog `source` Values

- `0` = `IO_SRC_ADS_INTERNAL_SINGLE`
- `1` = `IO_SRC_ADS_EXTERNAL_DIFF`
- `2` = `IO_SRC_DS18_WATER`
- `3` = `IO_SRC_DS18_AIR`

`channel` rules:

- ADS single: `0..3`
- ADS diff: `0` => diff `0-1`, `1` => diff `2-3`
- DS18B20: ignored

### 2.4 Analog Slot Configuration

Five configurable analog slots:

- `a0_*`, `a1_*`, `a2_*`, `a3_*`, `a4_*`

Each slot exposes:

- `aN_name`
- `aN_source`
- `aN_channel`
- `aN_c0`
- `aN_c1`
- `aN_prec`
- `aN_min`
- `aN_max`

Runtime endpoint IDs are stable (`a0..a4`).
Label names (`aN_name`) are display labels for MQTT/UI.

### 2.5 Digital Output Configuration

Each digital slot `dN` exposes:

- `dN_name`
- `dN_pin`
- `dN_active_high`
- `dN_initial_on`
- `dN_momentary`
- `dN_pulse_ms`

Notes:

- `dN_active_high` describes electrical polarity.
- `io.write` interprets logical ON/OFF, then maps to electrical level.
- Runtime output IDs are stable (`d0..d7`).
- `io.write` targets IDs like `d3`.
- `dN_momentary=true` enables pulse mode with `dN_pulse_ms` clamped to `1..60000 ms`.

### 2.6 Runtime Effect of `cfg/set`

Applied immediately:

- `pcf_enabled`
- `status_leds_mask` via runtime `io.write`

Applied after restart (persisted immediately in NVS):

- bus/driver settings (`i2c_*`, `ads_*`, `ds_poll_ms`, `pcf_address`, ...)
- analog endpoint definitions
- digital endpoint definitions
- `pcf_mask_default`

## 3) PCF8574 Mask Endpoint

When `pcf_enabled=true`, endpoint `status_leds_mask` is created.

- endpoint type: digital actuator
- value: 8-bit mask (`int32` transport)
- service ID: `io_leds`
- interface: `IOLedMaskService`
  - `setMask(mask)`
  - `turnOn(bit)`
  - `turnOff(bit)`
  - `getMask()`

## 4) Runtime DataStore

Runtime struct:

```cpp
struct IORuntimeData {
    IOEndpointRuntime endpoints[IO_MAX_ENDPOINTS];
};
```

Helpers in `IORuntime.h`:

- `setIoEndpointFloat(ds, idx, value, tsMs, dirtyMask)`
- `setIoEndpointBool(ds, idx, value, tsMs, dirtyMask)`
- `setIoEndpointInt(ds, idx, value, tsMs, dirtyMask)`

Data keys: `DATAKEY_IO_BASE + idx` (`DATAKEY_IO_BASE = 40`).

## 5) MQTT Runtime Snapshots

Published snapshots:

- `rt/io/input/aN` for analog inputs (`aN`)
- `rt/io/output/dN` for digital outputs (`dN`)

Each key is published as:

```json
{"name":"<label>","value":...}
```

with top-level timestamp `ts`.

## 6) Detailed ADS1115 Capture Chain (Binary Port -> MQTT)

This section documents the exact chain currently implemented in code for ADS1115-backed analog inputs.

### 6.1 End-to-End Stages

1. Scheduler tick (`ads_fast`) every `ads_poll_ms` (minimum enforced to `20 ms`).
2. ADS1115 async state machine:
   - if previous conversion is ready: read raw 16-bit signed value (`int16_t`)
   - convert raw to voltage
   - immediately request next conversion (round-robin channel/pair)
3. Analog definition processing (`aN`):
   - read `rawBinary` + `raw` voltage from driver cache
   - for ADS sources, continue only on a fresh per-channel/per-pair sample sequence
   - check valid range `[aN_min, aN_max]`
   - apply running median central average filter (`window=11`, `avgCount=5`)
   - apply calibration: `calibrated = aN_c0 * filtered + aN_c1`
   - apply precision rounding: `rounded = round(calibrated, aN_prec)`
4. Endpoint update (`AnalogSensorEndpoint`).
5. Domain callback (`onValueChanged`) only if rounded value changed.
6. DataStore write with `DIRTY_SENSORS`.
7. MQTTModule receives `DataSnapshotAvailable`, applies throttling (`sensor_min_publish_ms`), then publishes runtime snapshot on `rt/io/input/aN`.

### 6.2 Parameters Along the Chain

| Stage | Parameter | Effect |
|---|---|---|
| I2C/driver setup | `ads_internal_addr`, `ads_external_addr` | Selects ADS device addresses |
| ADS conversion | `ads_gain` | ADC full-scale and voltage-per-bit mapping |
| ADS conversion | `ads_rate` | ADS conversion speed index (`0..7`) |
| Scheduler cadence | `ads_poll_ms` | How often driver tick and analog processing run |
| Source routing | `aN_source`, `aN_channel` | Select internal single-ended channel or external differential pair |
| Input validation | `aN_min`, `aN_max` | Rejects out-of-range raw values before filtering |
| Calibration | `aN_c0`, `aN_c1` | Linear scaling (`y = c0*x + c1`) |
| Output quantization | `aN_prec` | Decimal precision used for runtime value and change detection |
| MQTT pacing | `mqtt.sensor_min_publish_ms` | Throttle for sensor publications |

Notes:
- `ads_rate` is passed directly to ADS1X15 library (`0..7` index).
- ADS1115 effective SPS by index (from library docs): `0:8`, `1:16`, `2:32`, `3:64`, `4:128`, `5:250`, `6:475`, `7:860`.
- In this firmware, scheduler cadence and channel multiplexing also determine effective per-channel freshness.

### 6.3 Binary Capture and Voltage Conversion Details

- Driver reads the ADC conversion register as signed `int16_t` (`rawBinary`).
- Voltage uses `toVoltage(rawBinary)` from ADS1X15.
- If library returns invalid voltage sentinel, fallback conversion is `rawBinary * voltLsb` (default `0.0001875 V/bit`).

This is the exact "binary port state -> physical value" transition used for logs and processing.

### 6.4 Running Median Behavior (Sample-by-Sample)

The filter helper is:
- `RunningMedianAverageFloat(windowSize=11, avgCount=5)`
- update rule per sample:
  - add new value to the 11-sample ring buffer
  - sort current window
  - return average of the 5 central sorted values (`getAverage(5)`)

So this is not a pure median output. It is a robust "central average around median".

#### Example timeline at synchronous cadence

Assume:
- `ads_poll_ms = 125 ms`
- internal ADS in single-ended mode (round-robin CH0 -> CH1 -> CH2 -> CH3)
- slot `a0` bound to CH0

Tick sequence:
1. Tick 1: CH0 value just completed -> read/store CH0, request CH1.
2. Tick 2: CH1 completed, request CH2.
3. Tick 3: CH2 completed, request CH3.
4. Tick 4: CH3 completed, request CH0.
5. Tick 5: next CH0 completed.

Implication for `a0`:
- processing function runs each tick, but the running median is updated only when CH0 has a fresh sample sequence.
- with `125 ms` tick, new CH0 physics sample period is about `500 ms` (`2 Hz`), and the filter update cadence for CH0 is the same `500 ms`.

#### Window evolution view

Let `xk` be successive *new* CH0 conversions.
Fresh-sample gating avoids reinjecting repeated cached values between conversions.

Typical evolution (conceptual):
- after first stable phase: `[x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10]`
- filter output = mean of central 5 sorted values

This keeps channel-level statistics physically meaningful while preserving robust outlier rejection.

### 6.5 Spike Rejection and Correction Profile

1. Hard out-of-range spike:
   - if raw is outside `[aN_min, aN_max]`, sample is rejected before filter, endpoint marked invalid.
2. In-range impulse spike (single conversion glitch):
   - central-average filter strongly attenuates it if it remains minority in window center.
3. Multiplexed-channel behavior:
   - each channel contributes one sample per real conversion only; no duplicate weighting from scheduler-only repetitions.
4. Real step changes:
   - treated as signal, not noise; filter follows with smoothing delay.

Operationally, this filter is best at removing short impulsive spikes and occasional outliers while keeping slow process values (pH/ORP/PSI) stable for MQTT publication.
