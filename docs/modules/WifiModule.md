# WifiModule (`moduleId: wifi`)

## Rôle

Gestion de la connectivité WiFi STA:
- machine d'états (`Disabled`, `Idle`, `Connecting`, `Connected`, `ErrorWait`)
- publication de l'état réseau dans `DataStore`
- exposition d'un service WiFi minimal

Type: module actif.

## Dépendances

- `loghub`
- `datastore`

## Affinité / cadence

- core: 0
- task: `wifi`
- délais d'état: 200ms à 2s selon état

## Services exposés

- `wifi` -> `WifiService`
  - `state`
  - `isConnected`
  - `getIP`

## Services consommés

- `datastore`
- `loghub`

## Config / NVS

Module config: `wifi` (`moduleId = ConfigModuleId::Wifi`, branche locale `1`)
- `enabled` (`wifi_en`)
- `ssid` (`wifi_ssid`)
- `pass` (`wifi_pass`)

## DataStore

Écritures via `WifiRuntime.h`:
- `setWifiReady(...)` -> `DataKeys::WifiReady`
- `setWifiIp(...)` -> `DataKeys::WifiIp`

## EventBus / MQTT

- pas d'abonnement EventBus
- pas de publication EventBus directe
- impact indirect: `MQTTModule` et `TimeModule` surveillent `DataKeys::WifiReady`
