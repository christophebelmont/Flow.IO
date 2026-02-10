# WifiModule

## General description

`WifiModule` manages STA connectivity (state transitions, reconnects, timeouts), exposes Wi-Fi status service, and publishes network runtime state to `DataStore`.

## Module dependencies

- `moduleId`: `wifi`
- Declared dependencies: `loghub`, `datastore`

## Provided services

- `wifi` -> `WifiService`
  - `state`
  - `isConnected`
  - `getIP`

## Consumed services

- `datastore` (`DataStoreService`) to publish runtime state
- `loghub` (`LogHubService`) for internal logging

## ConfigStore values used

- JSON module: `wifi`
- Keys:
  - `enabled`
  - `ssid`
  - `pass`

## DataStore values used

- Writes:
  - `wifi.ready` (DataKey `DATAKEY_WIFI_READY` = 1, dirty `DIRTY_NETWORK`)
  - `wifi.ip` (DataKey `DATAKEY_WIFI_IP` = 2, dirty `DIRTY_NETWORK`)
- Reads:
  - no business-level runtime reads
