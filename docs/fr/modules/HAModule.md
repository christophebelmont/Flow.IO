# HAModule

## General description

`HAModule` publishes Home Assistant auto-discovery entities (sensors, switches, config numbers) without blocking EventBus callbacks, using both ConfigStore metadata and runtime state.

## Module dependencies

- `moduleId`: `ha`
- Declared dependencies: `eventbus`, `config`, `datastore`, `mqtt`

## Provided services

- None.

## Consumed services

- `eventbus` (`EventBusService`) to consume `DataChanged`
- `config` (`ConfigStoreService`) for module/key introspection
- `datastore` (`DataStoreService`) to read/write HA runtime state
- `mqtt` (`MqttService`) to publish discovery topics

## ConfigStore values used

- JSON module: `ha`
- Keys:
  - `enabled`
  - `vendor`
  - `device_id`
  - `discovery_prefix`
  - `model`

## DataStore values used

- Writes:
  - `ha.autoconfigPublished` (DataKey `DATAKEY_HA_PUBLISHED` = 10, dirty `DIRTY_MQTT`)
  - `ha.vendor` (DataKey `DATAKEY_HA_VENDOR` = 11, dirty `DIRTY_MQTT`)
  - `ha.deviceId` (DataKey `DATAKEY_HA_DEVICE_ID` = 12, dirty `DIRTY_MQTT`)
- Reads:
  - `wifi.ready`
  - `mqtt.mqttReady`
  - `pool.devices[i].valid` to select `pool.write` vs `io.write` switch commands
