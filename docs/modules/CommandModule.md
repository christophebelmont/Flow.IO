# CommandModule (`moduleId: cmd`)

## Rôle

Expose la registry de commandes (`CommandRegistry`) au reste du système.

Type: module passif.

## Dépendances

- `loghub`

## Services exposés

- `cmd` -> `CommandService`
  - `registerHandler`
  - `execute`

## Services consommés

- `loghub` (log init)

## Config / NVS

Aucune variable `ConfigStore`.

## Capacité

La registry statique actuelle contient `MAX_COMMANDS = 24` entrées (`src/Core/CommandRegistry.h`).
`registerHandler(...)` retourne `false` si cette capacité est atteinte ou si une commande est déjà enregistrée.

## EventBus / DataStore / MQTT

Aucun direct.

## Commandes déclarées par les modules

Le `CommandModule` n'ajoute pas de commande métier lui-même; il sert d'infrastructure.
Commandes que les modules tentent d'enregistrer selon le profil compilé:
- `system.ping`, `system.reboot`, `system.factory_reset`
- `alarms.list`, `alarms.reset`, `alarms.reset_slot`, `alarms.reset_all`
- `time.resync`, `ntp.resync`
- `time.scheduler.info/get/set/clear/clear_all`
- `wifi.dump_cfg`
- `pooldevice.write`, `pool.write`, `pool.refill`
- `pooldevice.uptime.reset`, `pool.uptime.reset`
- `pooldevice.uptime.reset_all`, `pool.uptime.reset_all`
- `poollogic.filtration.write`, `poollogic.filtration.recalc`
- `poollogic.auto_mode.set`, `poollogic.auto_mode.toggle`
- `poollogic.ph_auto_mode.set/toggle`, `poollogic.orp_auto_mode.set/toggle`, `poollogic.winter_mode.set/toggle`
- `poollogic.filtration.toggle`
- `poollogic.ph_pump.write/toggle`, `poollogic.orp_pump.write/toggle`
- `poollogic.light.write/toggle`, `poollogic.lights.write/toggle`
- `poollogic.robot.write/toggle`, `poollogic.heater.write/toggle`
- `poollogic.chlorine_generator.write/toggle`, `poollogic.swg.write/toggle`
- `flow.system.reboot`, `flow.system.factory_reset` (profil `Supervisor`)
- `fw.update.status`, `fw.update.flowio`, `fw.update.supervisor`, `fw.update.nextion`
- `fw.nextion.reboot`, `fw.update.spiffs`, `fw.update.cfgdocs` (profil `Supervisor`)
