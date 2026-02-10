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

- `rt/io/input/state` for analog inputs (`aN`)
- `rt/io/output/state` for outputs (`dN`, `status_leds_mask`)

Each key is published as:

```json
{"name":"<label>","value":...}
```

with top-level timestamp `ts`.
