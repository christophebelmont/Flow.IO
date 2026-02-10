# MQTTModule

## General description

`MQTTModule` manages the MQTT session (connect, subscribe, publish), executes incoming commands (`cmd`), applies config patches (`cfg/set`), and routes runtime snapshots.

## Module dependencies

- `moduleId`: `mqtt`
- Declared dependencies: `loghub`, `wifi`

## Provided services

- `mqtt` -> `MqttService`
  - `publish`
  - `formatTopic`
  - `isConnected`

## Consumed services

- `wifi` (`WifiService`) for network context
- `cmd` (`CommandService`) to execute incoming command payloads
- `config` (`ConfigStoreService`) for config apply/export/listing
- `eventbus` (`EventBusService`) to consume:
  - `DataChanged`
  - `DataSnapshotAvailable`
  - `ConfigChanged`
- `datastore` (`DataStoreService`) for runtime reads and MQTT readiness state
- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- JSON module: `mqtt`
- Keys:
  - `host`
  - `port`
  - `user`
  - `pass`
  - `baseTopic`
  - `enabled`
  - `sensor_min_publish_ms`

## DataStore values used

- Writes:
  - `mqtt.mqttReady` (DataKey `DATAKEY_MQTT_READY` = 4, dirty `DIRTY_MQTT`)
- Reads:
  - `wifi.ready` for warmup/connect behavior
  - global runtime state through `DataStore*` for snapshot publishers
