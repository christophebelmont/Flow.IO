# SystemMonitorModule

## General description

`SystemMonitorModule` provides runtime health observability (heap, task stack watermark, Wi-Fi state, uptime) through periodic logs.

## Module dependencies

- `moduleId`: `sysmon`
- Declared dependencies: `loghub`

## Provided services

- None.

## Consumed services

- `wifi` (`WifiService`) for state/connection/IP
- `config` (`ConfigStoreService`) for potential config-level diagnostics
- `loghub` (`LogHubService`) for log emission

## ConfigStore values used

- No module-owned `ConfigStore` keys.

## DataStore values used

- No direct `DataStore` read/write.
