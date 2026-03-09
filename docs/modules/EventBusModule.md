# EventBusModule (`moduleId: eventbus`)

## Rôle

Hôte du bus d'événements interne `EventBus`.
- enregistre le service `eventbus`
- publie `SystemStarted` au démarrage
- exécute `dispatch(...)` en boucle

Type: module actif.

## Dépendances

- `loghub`

## Affinité / cadence

- core: `1`
- task name: `EventBus`
- stack: `4096`
- priority: `1`
- loop: `dispatch(16)` puis `delay(5 ms)`

## Services exposés

- `eventbus` -> `EventBusService`

## Capacités / limites

- longueur queue: `Limits::EventQueueLen` (`32`)
- payload max par événement: `48` octets
- abonnés max: `Limits::EventSubscribersMax` (`50`)
- ring interne de traces de rejets d'abonnement: `8`

## EventBus

Publication:
- `EventId::SystemStarted` dans `init()`

Abonnements:
- aucun (module hôte du bus)

## Logs EventBus (signification)

### Statistiques post (toutes les 5s)

`post stats 5s: drops=.. max_burst=.. ok_total=.. drop_total=.. isr=.. too_large=.. no_queue=..`
- niveau log: `DEBUG` par défaut, `WARN` si un compteur critique est non nul
- `drops`: drops dans la fenêtre 5s
- `max_burst`: plus grande rafale de drops dans la fenêtre
- `ok_total/drop_total`: cumuls boot
- `isr`: drops depuis `postFromISR`
- `too_large`: payload > 48 octets
- `no_queue`: queue indisponible

### Statistiques abonnements (toutes les 5s)

`sub stats 5s: used=../50 queue=../32 data=.. cfg=.. sched=.. alarm=.. other=.. rej_total=.. cap=.. null_cb=..`
- niveau log: `DEBUG` par défaut, `WARN` si rejet d'abonnement (`rej_total/cap/null_cb > 0`) ou queue pleine
- `used`: nombre d'abonnements actifs
- `queue`: occupation instantanée queue
- `data/cfg/sched/alarm/other`: répartition par type d'événement
- `rej_total`: rejets cumulés d'abonnement
- `cap`: rejets pour capacité atteinte
- `null_cb`: rejets callback nulle

### Détails rejet d'abonnement

`sub reject seq=.. event=.. reason=capacity|null_cb cb=0x.. user=0x.. age_ms=..`
- trace détaillée des tentatives `subscribe(...)` rejetées
- imprimée depuis le ring interne (taille 8)

### Warnings perf

- `dispatch slow: ...` : batch `dispatch` trop long
- `slow handler: ...` : callback abonné trop long

## DataStore / MQTT

Aucun accès direct.
Le module sert de transport interne pour tous les flux `DataChanged`, `ConfigChanged`, alarmes, scheduler, etc.
