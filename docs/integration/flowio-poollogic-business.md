# FlowIO — logique métier piscine (contexte de compilation `flowio`)

Ce document résume la logique métier réellement compilée dans le profil **FlowIO** (board `FlowIOBoardRev1`, domain `Pool`).  
Il complète la doc module par module avec une vue orientée fonctionnement bassin (filtration, hiver, injection pH/ORP, électrolyse, robot, remplissage).

## 1) Contexte de compilation `flowio`

Le profil `FlowIO` assemble:

- Board: `FlowIOBoardRev1`
- Domaine: `Pool`
- Modules critiques métier: `IOModule`, `PoolLogicModule`, `PoolDeviceModule`, `TimeModule`, `AlarmModule`, `CommandModule`, `MQTTModule`, `HAModule`.

Le flux de décision principal est:

1. `PoolLogicModule` calcule les **intentions métier** (désirs ON/OFF des équipements + régulation PID).
2. `PoolDeviceModule` applique ces demandes avec les **interlocks** (dépendances, max uptime, activation appareil, I/O error), puis pilote les sorties via `IOModule`.
3. `IOModule` est l’abstraction des entrées/sorties physiques de la carte Rev1.

## 2) Noms E/S par défaut (board revision 1)

### 2.1 Sorties physiques (DO)

Sur la board Rev1, les sorties sont câblées par défaut ainsi:

- `relay1` → Filtration pump
- `relay2` → Pompe pH
- `relay3` → Pompe chlore liquide (ORP peristaltique)
- `relay4` → Électrolyseur (momentary/pulse sur cette carte)
- `relay5` → Robot
- `relay6` → Éclairage
- `relay7` → Pompe de remplissage
- `relay8` → Chauffage

Côté identifiants IO runtime compatibles pool:

- `d0=filtration`, `d1=ph`, `d2=chlore`, `d3=electrolyse`, `d4=robot`, `d5=lights`, `d6=fill`, `d7=heater`

### 2.2 Entrées capteurs (AI/DI)

Affectation logique pool par défaut:

- Analogiques:
  - `a0` ORP
  - `a1` pH
  - `a2` PSI
  - `a3` Spare
  - `a4` Température eau
  - `a5` Température air
- Digitales:
  - `i0` Niveau bassin
  - `i1` Niveau cuve pH
  - `i2` Niveau cuve chlore
  - `i3` Compteur eau

## 3) Rôles des 3 modules clés

## 3.1 `PoolLogicModule` (orchestrateur métier)

Responsabilités:

- calcule la fenêtre de filtration (selon température eau + bornes horaires)
- arbitre mode auto/manuel/hiver
- applique sécurités pression PSI
- pilote robot, électrolyse, remplissage
- exécute la régulation temporelle PID pH et ORP (pompes péristaltiques)
- publie la configuration `cfg/poollogic/*` et snapshots `rt/poollogic/ph|orp`

Important: en mode manuel (`auto_mode=false`), la filtration reste pilotée manuellement **sauf sécurité PSI** (qui garde priorité et peut couper).

## 3.2 `PoolDeviceModule` (device manager / couche d’exécution)

Responsabilités:

- expose le service `PoolDeviceService` utilisé par `PoolLogic`
- applique les commandes ON/OFF demandées sur slots `pd0..pd7`
- impose des contraintes d’exécution:
  - `enabled` par appareil
  - dépendances (`depends_on_mask`)
  - `max_uptime_day_s`
  - état I/O réel
- maintient compteurs runtime:
  - temps de marche jour/semaine/mois/total
  - volume injecté jour/semaine/mois/total
  - volume restant cuve
- persiste les métriques (`metrics_blob`) dans ConfigStore

## 3.3 `IOModule`

Responsabilités:

- lit les analogiques/digitales utilisées par la logique pool
- écrit les sorties digitales physiques
- maintient la config des bindings (`binding_port`) + calibration analogique (`c0`,`c1`,`precision`) + logique d’entrée digitale (actif haut, pull, edge/counter)

## 4) Modes métier (auto, hiver, manuel)

## 4.1 Auto global (`auto_mode=true`)

- filtration demandée par:
  - fenêtre scheduler calculée
  - ou mode hiver + condition de température air
  - avec logique de maintien hors-gel (`freeze_hold_t`) si la filtration tourne déjà
- robot, électrolyse, PID pH/ORP sont pilotés automatiquement

## 4.2 Manuel (`auto_mode=false`)

- `PoolLogic` n’impose plus de logique auto de filtration
- un ordre manuel filtration (`poollogic.filtration.write`) force explicitement `auto_mode=false`
- les sécurités PSI restent actives (coupure possible)

## 4.3 Hiver (`winter_mode=true`)

- ajout d’une demande de filtration quand `air_temp < winter_start_t`
- logique hors-gel: si filtration déjà active et `air_temp <= freeze_hold_t`, elle reste maintenue

## 5) Détail des équipements

## 5.1 Filtration (pompe principale)

Décision finale (priorité):

1. sécurité PSI (stop)
2. manuel (conserver état manuel)
3. auto (fenêtre de filtration / hiver / freeze-hold)

La fenêtre est recalculée quotidiennement et peut être relancée via commande.

## 5.2 Injection pH (pompe pH)

Conditions d’autorisation:

- filtration demandée ON
- PID pH armé (`ph_auto_mode` + délai post-filtration)
- mesure pH disponible
- pas d’erreur PSI
- cuve pH non vide

Le PID est temporel (sortie en `outputOnMs` dans une fenêtre), avec minimum ON configurable (`pid_min_on_ms`).

`ph_dose_plus` inverse le sens d’erreur (injection pH+ vs pH-).

## 5.3 Injection ORP / chlore liquide (pompe ORP)

Conditions d’autorisation:

- filtration demandée ON
- PID ORP armé (`orp_auto_mode` + délai post-filtration)
- mesure ORP disponible
- pas d’erreur PSI
- cuve chlore non vide
- **et** `elec_mode=false` (la pompe ORP est inhibée quand électrolyse active)

## 5.4 Électrolyseur

En auto:

- nécessite filtration ON
- nécessite `elec_mode=true`
- démarrage autorisé si:
  - température eau >= `secure_elec_t`
  - délai filtration >= `dly_electro_min`
- si `elec_run=true`, l’électrolyse est asservie à ORP (hystérésis de démarrage à 90% de la consigne)

## 5.5 Robot

En auto:

- ne démarre que si filtration ON et nettoyage non fait du jour
- démarre après `robot_delay_min`
- s’arrête après `robot_dur_min`
- `cleaning_done` est remis à false au changement de jour

## 5.6 Remplissage

- basé sur capteur niveau bassin (`i0`)
- si niveau bas: marche
- quand niveau revient OK: maintien ON jusqu’à atteindre au moins `fill_min_on_s`

## 6) Sécurité / alarmes

Alarmes métier déclarées:

- `PoolPsiLow` (pression basse)
- `PoolPsiHigh` (pression haute)
- `PoolPhTankLow`
- `PoolChlorineTankLow`

Si le service alarme n’est pas disponible, `PoolLogic` utilise un fallback local conservatif (latch PSI local).

## 7) Variables ConfigStore importantes

## 7.1 `poollogic/*`

### `poollogic/mode`

- `enabled`
- `auto_mode`
- `winter_mode`
- `ph_auto_mode`
- `orp_auto_mode`
- `ph_dose_plus`
- `elec_mode`
- `elec_run`

### `poollogic/filtration`

- `wat_temp_lo_th`
- `wat_temp_setpt`
- `filtr_start_min`
- `filtr_stop_max`
- `filtr_start_clc`
- `filtr_stop_clc`

### `poollogic/sensors`

- `ph_io_id`
- `orp_io_id`
- `psi_io_id`
- `wat_temp_io_id`
- `air_temp_io_id`
- `pool_lvl_io_id`
- `ph_lvl_io_id`
- `chl_lvl_io_id`

### `poollogic/pid`

- `psi_low_th`
- `psi_high_th`
- `winter_start_t`
- `freeze_hold_t`
- `secure_elec_t`
- `ph_setpoint`
- `orp_setpoint`
- `ph_kp`, `ph_ki`, `ph_kd`
- `orp_kp`, `orp_ki`, `orp_kd`
- `ph_window_ms`
- `orp_window_ms`
- `pid_min_on_ms`
- `pid_sample_ms`

### `poollogic/delay`

- `psi_start_dly_s`
- `dly_pid_min`
- `dly_electro_min`
- `robot_delay_min`
- `robot_dur_min`
- `fill_min_on_s`

### `poollogic/device`

- `filtr_slot`
- `swg_slot`
- `robot_slot`
- `fill_slot`
- `ph_pump_slot`
- `orp_pump_slot`

## 7.2 `pdm/*` (device manager)

Par slot `pdm/pdN` (N=0..7):

- `enabled`
- `depends_on_mask`
- `flow_l_h`
- `tank_cap_ml`
- `tank_init_ml`
- `max_uptime_day_s`

Runtime persistant par slot `pdmrt/pdN`:

- `metrics_blob`

## 7.3 `io/*` (principalement utile à la logique piscine)

- activation globale: `io/enabled`
- capteurs analogiques `io/input/a0..a5`:
  - `*_name`, `binding_port`, `*_c0`, `*_c1`, `*_prec`
- entrées digitales `io/input/i0..i4`:
  - `*_name`, `binding_port`, `*_active_high`, `*_pull_mode`, `edge_mode`
- sorties `io/output/d0..d7`:
  - `*_name`, `binding_port`, `*_active_high`, `*_initial_on`, `*_momentary`, `*_pulse_ms`

## 8) Commandes métier principales

- `poollogic.filtration.write`:
  - force `auto_mode=false`
  - écrit directement la demande filtration
- `poollogic.filtration.recalc`:
  - demande recalcul de la fenêtre de filtration
- `poollogic.auto_mode.set`:
  - bascule explicite mode auto
- `pooldevice.write`:
  - commande directe d’un slot appareil
- `pool.refill`:
  - réinitialise niveau cuve (pompes doseuses)

## 9) Résumé opérationnel

- `PoolLogic` décide **quoi faire** (métier bassin).  
- `PoolDevice` décide **si c’est autorisé** et applique **comment** côté actionneurs.  
- `IO` traduit en lectures capteurs / écritures relais de la board Rev1.

Cette séparation permet:

- une logique métier claire et configurable (`poollogic/*`)
- une sécurité d’exécution centralisée (`pdm/*`)
- une adaptation hardware propre (`io/*` + bindings FlowIO Rev1).
