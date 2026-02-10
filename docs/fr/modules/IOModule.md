# IOModule

## General description

`IOModule` is the hardware abstraction layer for Flow.IO: buses (I2C/OneWire), drivers (ADS1115/DS18B20/GPIO/PCF8574), endpoints, polling scheduler, and runtime command handling (`io.write`).

## Module dependencies

- `moduleId`: `io`
- Declared dependencies: `loghub`, `datastore`, `cmd`, `mqtt`
- Note: `mqtt` dependency is mostly structural (startup ordering), not direct service usage.

## Provided services

- `io_leds` -> `IOLedMaskService` (only when PCF8574 is active)
  - `setMask`
  - `turnOn`
  - `turnOff`
  - `getMask`

## Consumed services

- `cmd` (`CommandService`) to register `io.write`
- `datastore` (`DataStoreService`) to publish endpoint runtime updates
- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- JSON module: `io`
  - `enabled`
  - `i2c_sda`, `i2c_scl`
  - `ads_poll_ms`, `ds_poll_ms`
  - `ads_internal_addr`, `ads_external_addr`
  - `ads_gain`, `ads_rate`
  - `pcf_enabled`, `pcf_address`, `pcf_mask_default`, `pcf_active_low`
- Analog JSON modules: `io/input/a0` ... `io/input/a4`
  - `aN_name`, `aN_source`, `aN_channel`, `aN_c0`, `aN_c1`, `aN_prec`, `aN_min`, `aN_max`
- Digital JSON modules: `io/output/d0` ... `io/output/d7`
  - `dN_name`, `dN_pin`, `dN_active_high`, `dN_initial_on`, `dN_momentary`, `dN_pulse_ms`

## DataStore values used

- Writes:
  - `io.endpoints[idx]` via `setIoEndpoint*` helpers
  - DataKey range: `DATAKEY_IO_BASE + idx` (`DATAKEY_IO_BASE = 40`)
  - Dirty flags:
    - `DIRTY_ACTUATORS` for command-side writes (`io.write`)
    - `DIRTY_SENSORS` for measurement-side updates from integration callbacks
- Reads:
  - `io.endpoints[]` when building runtime snapshots
- Integration note:
  - analog values are pushed to DataStore via `onValueChanged` callbacks wired by integrator code (for example in `main.cpp`).
