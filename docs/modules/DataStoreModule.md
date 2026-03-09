# DataStoreModule (`moduleId: datastore`)

## Rôle

Expose l'instance partagée `DataStore` et la connecte à `EventBus`.

Type: module passif.

## Dépendances

- `eventbus`

## Services exposés

- `datastore` -> `DataStoreService` (`DataStore* store`)

## Services consommés

- `eventbus` pour `DataStore::setEventBus(...)`

## Config / NVS

Aucune variable `ConfigStore`.

## EventBus / DataStore

Le module ne publie pas directement d'événement.
Mais après wiring:
- toute écriture runtime via helpers `*Runtime.h` appelant `DataStore::notifyChanged` émet:
  - `DataChanged` (avec `DataKey`)

## MQTT

Aucune publication directe.
Les changements runtime passent par `DataChanged` et sont consommés par le cœur MQTT unifié (`MQTTModule` + `RuntimeProducer`).
