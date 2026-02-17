# HAModule (`moduleId: ha`)

## Rôle

Publication Home Assistant MQTT Discovery:
- registre d'entités statiques (sensor, binary_sensor, switch, number, button)
- publication discovery retainée
- refresh automatique sur changements runtime pertinents

Type: module actif (event-driven par notification task).

## Dépendances

- `eventbus`
- `config`
- `datastore`
- `mqtt`

## Affinité / cadence

- core: 0
- task: `ha`
- loop bloquant sur notification (`ulTaskNotifyTake`)

## Services exposés

- `ha` -> `HAService`
  - `addSensor`, `addBinarySensor`, `addSwitch`, `addNumber`, `addButton`
  - `requestRefresh`

## Services consommés

- `eventbus`
- `datastore`
- `mqtt`

## Config / NVS

Branche `ConfigBranchId::Ha` (`module: ha`):
- `enabled`
- `vendor`
- `device_id`
- `disc_prefix`
- `model`

## DataStore

Écritures via `HARuntime.h`:
- `HaPublished`
- `HaVendor`
- `HaDeviceId`

## EventBus

Abonnement:
- `DataChanged`

Réactions:
- sur `WifiReady`/`MqttReady` -> tentative/pending de publication auto-discovery

## MQTT

- construit topics discovery:
  - `<discoveryPrefix>/<component>/<nodeTopicId>/<objectId>/config`
- payloads incluent:
  - device metadata
  - `availability` basée sur topic `status`
- capteurs diagnostic natifs publiés:
  - `alarms_pack` (`rt/alarms/p`)
  - `uptime_seconds` (`rt/system/state`, valeur `upt_s`)

## Comportement refresh

- `add*` met à jour la table d'entités et demande refresh
- `requestRefresh` remet `published=false` et notifie la task
- publication effective seulement si MQTT connecté et `mqttReady(DataStore)==true`
