# PoolLogicModule (`moduleId: poollogic`)

## Rôle

`PoolLogicModule` est l'orchestrateur métier principal de la piscine.  
Il:
- calcule et maintient la fenêtre quotidienne de filtration à partir de la température d'eau
- pilote les équipements via `PoolDeviceService` (filtration, pompe pH, pompe ORP/chlore liquide, robot, électrolyse, remplissage)
- applique les règles automatiques (modes, seuils, délais, sécurités)
- exécute la régulation pH/ORP en **PID temporel** (duty-cycle dans une fenêtre fixe)
- expose des snapshots runtime MQTT (`rt/poollogic/ph`, `rt/poollogic/orp`)
- enregistre des commandes métier et des entités Home Assistant
- surveille la pression via `AlarmService`

Type: module actif.

## Dépendances

- `loghub`
- `eventbus`
- `time` (`TimeSchedulerService`)
- `io` (`IOServiceV2`)
- `pooldev` (`PoolDeviceService`)
- `ha` (`HAService`)
- `cmd` (`CommandService`)
- `alarms` (`AlarmService`)

## Affinité / cadence

- core: `1`
- task: `poollogic`
- loop: `200 ms` (`vTaskDelay(pdMS_TO_TICKS(200))`)

## Interfaces exposées

Services Core exposés:
- aucun service public direct

Interfaces runtime exposées:
- `IRuntimeSnapshotProvider`
  - `rt/poollogic/ph`
  - `rt/poollogic/orp`

Ces snapshots sont routés vers MQTT via le runtime mux (`main.cpp`), pas publiés directement par `PoolLogicModule`.

## Config / NVS

Branche: `ConfigBranchId::PoolLogic`  
Module JSON: `poollogic`  
Persistance: `ConfigStore` + `NvsKeys::PoolLogic::*`

### Paramètres modes et stratégie

- `enabled`
- `auto_mode`
- `winter_mode`
- `ph_auto_mode`
- `orp_auto_mode`
- `electrolys_mode`
- `electro_run_md`

### Fenêtre de filtration (calcul quotidien)

- `wat_temp_lo_th`
- `wat_temp_setpt`
- `filtr_start_min`
- `filtr_stop_max`
- `filtr_start_clc` (calculé)
- `filtr_stop_clc` (calculé)

### Bindings capteurs IO

- `ph_io_id`
- `orp_io_id`
- `psi_io_id`
- `wat_temp_io_id`
- `air_temp_io_id`
- `pool_lvl_io_id`

### Seuils métier

- `psi_low_th`
- `psi_high_th`
- `winter_start_t`
- `freeze_hold_t`
- `secure_elec_t`
- `ph_setpoint`
- `orp_setpoint`

### PID pH / ORP (temporel)

- `ph_kp`, `ph_ki`, `ph_kd`
- `orp_kp`, `orp_ki`, `orp_kd`
- `ph_window_ms`
- `orp_window_ms`
- `pid_min_on_ms`
- `pid_sample_ms`

### Délais / temporisations

- `psi_start_dly_s`
- `delay_pids_min`
- `dly_electro_min`
- `robot_delay_min`
- `robot_dur_min`
- `fill_min_on_s`

### Slots équipements (PoolDevice)

- `filtration_slot`
- `swg_slot`
- `robot_slot`
- `filling_slot`

## Commandes

Enregistrées via `CommandService`:

- `poollogic.filtration.write`
  - args: `{"value":true|false}`
  - force `auto_mode=false`
  - écrit l'état désiré du slot filtration via `pooldev.writeDesired`

- `poollogic.filtration.recalc`
  - met en file une recomputation de la fenêtre
  - traitement asynchrone dans la loop

- `poollogic.auto_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `auto_mode`

Les réponses d'erreur suivent `ErrorCode` (`MissingArgs`, `MissingValue`, `NotReady`, `Disabled`, `InterlockBlocked`, etc.).

## EventBus et Scheduler

### Abonnements EventBus

- `EventId::SchedulerEventTriggered`
  - `POOLLOGIC_EVENT_DAILY_RECALC` + `Trigger` -> queue recalc de filtration
  - `TIME_EVENT_SYS_DAY_START` + `Trigger` -> reset quotidien interne (`cleaningDone_ = false`)
  - `POOLLOGIC_EVENT_FILTRATION_WINDOW` + `Start/Stop` -> mise à jour `filtrationWindowActive_`

### Slots scheduler gérés

- slot `3` (`POOLLOGIC_EVENT_DAILY_RECALC`)
  - rappel quotidien à 15:00
- slot `4` (`POOLLOGIC_EVENT_FILTRATION_WINDOW`)
  - fenêtre start/stop calculée dynamiquement
  - `replayStartOnBoot=true` pour reconstruire l'état après reboot

## Algorithme de contrôle

### Entrées runtime

- capteurs analogiques: pH, ORP, PSI, température eau, température air
- capteur digital: niveau bassin
- états actionneurs: lecture `pooldev.readActualOn(...)`
- états alarmes PSI: via `alarmSvc->isActive(...)`

### Pilotage filtration / robot / SWG / remplissage

Logique principale:
- filtration:
  - en auto: suit fenêtre scheduler, mode hiver, freeze-hold, et coupe sur erreur PSI
  - en manuel (`auto_mode=false`): `PoolLogic` n'impose pas de demande auto
- robot:
  - démarre après `robot_delay_min` de filtration
  - s'arrête après `robot_dur_min`
  - un cycle/jour (`cleaningDone_`)
- électrolyse:
  - nécessite filtration active, température mini (`secure_elec_t`) et délai (`dly_electro_min`)
  - en mode `electro_run_md`, asservissement ORP avec hystérésis implicite (`<= 100%` pour maintenir, `<= 90%` pour démarrer)
- remplissage:
  - démarre si niveau bas
  - respecte un minimum de marche `fill_min_on_s`

### Alarmes pression PSI

- `AlarmId::PoolPsiLow`
  - latched
  - délai ON `2000 ms`, OFF `1000 ms`, répétition `60000 ms`
- `AlarmId::PoolPsiHigh`
  - latched
  - sévérité critique
  - délai ON `0 ms`, OFF `1000 ms`, répétition `60000 ms`

## Régulation PID temporelle (pH / ORP)

La régulation est implémentée dans `PoolLogicModule` avec deux états internes (`TemporalPidState`), un pour pH, un pour ORP.

### Activation

Le mode PID est autorisé seulement si:
- filtration en marche
- pas de mode hiver
- délai de stabilisation atteint (`delay_pids_min`)
- mode auto de la boucle activé (`ph_auto_mode` / `orp_auto_mode`)

Le calcul et la commande sont ensuite conditionnés par:
- capteur disponible (`ph_io_id` / `orp_io_id`)
- pas de défaut PSI latched
- pour ORP péristaltique: `electrolys_mode == false` (en mode électrolyse, la pompe ORP liquide est inhibée)

### Convention d'erreur

- pH: `error = ph_input - ph_setpoint`
  - injection acide seulement si `error > 0`
- ORP: `error = orp_setpoint - orp_input`
  - injection chlore liquide seulement si `error > 0`

### Calcul périodique

À chaque `pid_sample_ms` (par défaut `30000 ms`):
- intégrale:
  - accumulée si `Ki != 0`
  - remise à zéro si `Ki == 0` ou `error <= 0`
- dérivée:
  - `dE/dt` entre l'échantillon précédent et courant
- sortie:
  - `u = Kp*e + Ki*I + Kd*D`
  - clampée sur `[0, window_ms]`
  - filtrée par seuil minimal: si `u < pid_min_on_ms`, alors `0`

### Fenêtre temporelle (PWM temporel)

Chaque boucle a une fenêtre cyclique fixe (`ph_window_ms` / `orp_window_ms`):
- la fenêtre est avancée par pas de `window_ms`
- `output_on_ms` représente la durée ON au début de la fenêtre
- demande pompe:
  - ON si `elapsed_in_window < output_on_ms`
  - OFF sinon

### Reset état PID

Si les conditions d'autorisation ne sont plus remplies:
- sortie forcée à `0`
- demande OFF
- reset de l'état interne (fenêtre, intégrale, erreur, échantillons)

### Actionnement

Le module n'écrit pas directement les GPIO.  
Il passe par `PoolDeviceService::writeDesired` sur:
- slot pH (`POOL_IO_SLOT_PH_PUMP`)
- slot ORP/chlore liquide (`POOL_IO_SLOT_CHLORINE_PUMP`)

Les interlocks `PoolDeviceModule` restent appliqués.

## Runtime MQTT (`rt/poollogic/*`)

Snapshots publiés (via runtime mux):
- `rt/poollogic/ph`
- `rt/poollogic/orp`

Payload (champs principaux):
- `id`: `ph` ou `orp`
- `input`: valeur **échantillonnée lors du dernier compute PID** (pas la mesure live instantanée)
- `setpoint`: setpoint utilisé lors du dernier compute
- `error`: erreur utilisée lors du dernier compute
- `compute_ts`: timestamp du dernier compute PID
- `enabled`: boucle autorisée côté logique
- `demand`: demande ON/OFF calculée dans la fenêtre
- `actual`: état ON/OFF réel lu sur le device
- `kp`, `ki`, `kd`
- `window_ms`, `sample_ms`, `min_on_ms`
- `output_on_ms`
- `window_elapsed_ms`
- `electrolyse_mode`
- `ts`: timestamp snapshot

Sémantique importante:
- `input/setpoint/error` sont des valeurs latched au compute PID
- `rt/io/input/*` reste la source des mesures live brutes

## Home Assistant

Entités enregistrées par `PoolLogicModule`:
- switches:
  - `pool_auto_mode` (`Pool Auto-regulation`)
  - `pool_winter_mode`
  - `pool_ph_auto_mode` (`pH Auto-regulation`)
  - `pool_orp_auto_mode` (`Orp Auto-regulation`)
- sensors:
  - `calculated_filtration_start`
  - `calculated_filtration_stop`
- numbers (section configuration):
  - `delay_pids_min`
  - `ph_setpoint`
  - `orp_setpoint`
  - `ph_pid_window_min` (conversion vers `ph_window_ms`)
  - `orp_pid_window_min` (conversion vers `orp_window_ms`)
  - `psi_low_threshold`
  - `psi_high_threshold`
- button:
  - `filtration_recalc` -> `{"cmd":"poollogic.filtration.recalc"}`

## DataStore / EventStore

- pas d'écriture directe `DataStore` par `PoolLogicModule`
- interactions runtime via services (`IOServiceV2`, `PoolDeviceService`, `AlarmService`)
- pas d'`EventStore` persistant côté `PoolLogic` (événements runtime via `EventBus`, config persistante via `ConfigStore`)
