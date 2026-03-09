# HAModule (`moduleId: ha`)

## Rôle

Publication Home Assistant MQTT Discovery:
- registre d'entités statiques (sensor, binary_sensor, switch, number, button)
- publication discovery retainée
- refresh automatique sur changements runtime pertinents
- support du champ discovery `has_entity_name` sur les sensors (piloté par l'entité appelante)

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

Module config: `ha` (`moduleId = ConfigModuleId::Ha`, branche locale `1`):
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

- producteur MQTT enregistré (`producerId` dédié HA) sur le cœur TX unifié
- jobs HA en mode IDs (`producerId + messageId`), sans publication directe module -> broker
- build topic/payload discovery à la demande via `MqttBuildContext` (buffer central MQTT)
- construit topics discovery:
  - `<discoveryPrefix>/<component>/<nodeTopicId>/<objectId>/config`
- préfixe `object_id` de type `flowioNNN_*`:
  - `NNN` dérive du `deviceId` MQTT effectif (`mqtt.topicDeviceId` / `mq_tid` si défini, sinon fallback auto)
- payloads incluent:
  - device metadata
  - `device.identifiers[0] = <vendor>-<mqttDeviceIdEffectif>` (donc suit `mqtt.topicDeviceId` / `mq_tid` si défini)
  - `availability` basée sur topic `status`
- capteurs diagnostic natifs publiés:
  - `alarms_pack` (`rt/alarms/p`)
  - `uptime_seconds` (`rt/system/state`, valeur `upt_s`)
  - `heap_free_bytes` (`rt/system/state`, conversion en `ko`)
  - `heap_min_free_bytes` (`rt/system/state`, conversion en `ko`)
  - `heap_fragmentation` (`rt/system/state`, valeur `%`)

## Comportement refresh

- `add*` met à jour la table d'entités et demande refresh
- `requestRefresh` remet `published=false` et notifie la task
- publication effective seulement si MQTT connecté et `mqttReady(DataStore)==true`
