# IOModule (`moduleId: io`)

## Rôle

`IOModule` est la couche d'entrées/sorties du firmware `FlowIO`.

Le module regroupe actuellement:

- l'inventaire des endpoints IO exposés aux autres modules
- les drivers GPIO, ADS1115, DS18B20 et PCF8574
- le scheduler d'acquisition
- la persistance et l'application de la configuration IO
- les snapshots runtime pour MQTT
- les valeurs Runtime UI liées aux mesures principales
- la synchronisation Home Assistant des capteurs et sorties déclarés par le profil

Le service public exposé au reste du firmware est `IOServiceV2`.

Type: module actif.

## Dépendances

En build `FlowIO`, les dépendances déclarées sont:

- `loghub`
- `datastore`
- `mqtt`

Le profil `FlowIO` lui associe en plus:

- les bus 1-Wire `oneWireWater` et `oneWireAir`
- la table de ports physiques définie dans `src/Profiles/FlowIO/FlowIOIoLayout.h`
- les définitions d'endpoints construites dans `src/Profiles/FlowIO/FlowIOIoAssembly.cpp`

## Affinité et cadence

- core: `1`
- task: `io`
- stack: `2560`
- boucle: `10 ms`

Jobs planifiés en interne:

- `ads_fast`
- `ds_slow`
- `din_poll`

## Services exposés

- `io` -> `IOServiceV2`
- `status_leds` -> `StatusLedsService`

Fonctions principales de `IOServiceV2`:

- inventaire des endpoints: `count`, `idAt`, `meta`
- lecture générique: `readValue`
- lecture digitale: `readDigital`
- écriture digitale: `writeDigital`
- lecture analogique: `readAnalog`
- suivi du cycle IO: `tick`, `lastCycle`

## Capacités statiques

Capacités compile-time actuelles dans `src/Modules/IOModule/IOModule.h`:

| Capacité | Valeur |
|---|---:|
| entrées analogiques | 12 |
| entrées digitales | 5 |
| sorties digitales | 10 |
| slots digitaux totaux | 15 |

Plages d'`IoId` utilisées:

- sorties digitales: `IO_ID_DO_BASE .. IO_ID_DO_BASE + 9`
- entrées digitales: `IO_ID_DI_BASE .. IO_ID_DI_BASE + 4`
- entrées analogiques: `IO_ID_AI_BASE .. IO_ID_AI_BASE + 11`

## Backends physiques pris en charge

Backends actuellement gérés par le module:

- GPIO direct
- sorties via `PCF8574`
- entrées analogiques `ADS1115`
- sondes `DS18B20`
- compteur d'impulsions sur GPIO avec debounce

Les `IoBackend` visibles dans le service sont:

- `IO_BACKEND_GPIO`
- `IO_BACKEND_PCF8574`
- `IO_BACKEND_ADS1115_INT`
- `IO_BACKEND_ADS1115_EXT_DIFF`
- `IO_BACKEND_DS18B20`

## Modèle de binding actuel

Le module ne déduit pas seul le câblage métier. Le binding est fourni par le profil `FlowIO`.

### Catalogue des ports physiques

`src/Profiles/FlowIO/FlowIOIoLayout.h` déclare:

- les `PhysicalPortId`
- la table `kBindingPorts`
- les bindings par défaut des rôles analogiques
- les bindings par défaut des entrées digitales
- les bindings par défaut des sorties digitales

Ports déclarés actuellement:

- ADS interne: `PortAdsInternal0..3`
- ADS externe différentiel: `PortAdsExternal0..1`
- DS18B20: `PortDsWater`, `PortDsAir`
- entrées digitales GPIO: `PortDigitalIn1..4`
- sorties relais GPIO: `PortRelay1..8`
- sorties PCF8574: `PortPcf0Bit0..7`

### Instanciation des endpoints

`src/Profiles/FlowIO/FlowIOIoAssembly.cpp` lit le domaine actif et appelle:

- `defineAnalogInput`
- `defineDigitalInput`
- `defineDigitalOutput`

Le profil `FlowIO` instancie aujourd'hui:

- 6 entrées analogiques
- 4 entrées digitales
- 8 sorties digitales

## Affectation actuelle des rôles `FlowIO`

### Entrées analogiques

| Rôle par défaut | Port physique par défaut |
|---|---|
| `OrpSensor` | `PortAdsInternal0` |
| `PhSensor` | `PortAdsInternal1` |
| `PsiSensor` | `PortAdsInternal2` |
| `SpareAnalog` | `PortAdsInternal3` |
| `WaterTemp` | `PortDsWater` |
| `AirTemp` | `PortDsAir` |

### Entrées digitales

| Rôle par défaut | Port physique par défaut | Mode |
|---|---|---|
| `PoolLevelSensor` | `PortDigitalIn1` | état |
| `PhLevelSensor` | `PortDigitalIn2` | état |
| `ChlorineLevelSensor` | `PortDigitalIn3` | état |
| `WaterCounterSensor` | `PortDigitalIn4` | compteur, front montant, debounce `100000 us` |

### Sorties digitales

| Rôle par défaut | Port physique par défaut |
|---|---|
| `FiltrationPump` | `PortRelay1` |
| `PhPump` | `PortRelay2` |
| `ChlorinePump` | `PortRelay3` |
| `ChlorineGenerator` | `PortRelay4` |
| `Robot` | `PortRelay5` |
| `Lights` | `PortRelay6` |
| `FillPump` | `PortRelay7` |
| `WaterHeater` | `PortRelay8` |

## Configuration et NVS

Structures de configuration utilisées:

- `IOModuleConfig`
- `IOAnalogSlotConfig`
- `IODigitalInputSlotConfig`
- `IODigitalOutputSlotConfig`

Branches de configuration exposées par le module:

- `Io`
- `IoDebug`
- `IoInputA0 .. IoInputA5`
- `IoOutputD0 .. D7`
- `IoInputD0 .. D3`

Paramètres principaux:

- activation module: `enabled`
- bus I2C principal: `i2c_sda`, `i2c_scl`
- polling: `ads_poll_ms`, `ds_poll_ms`, `digital_poll_ms`
- ADS: adresses, gain, rate
- PCF8574: enable, adresse, masque par défaut, logique active low
- traces: `trace_enabled`, `trace_period_ms`
- calibration et précision des entrées analogiques
- binding, `activeHigh`, `momentary`, `pulseMs` des sorties digitales
- binding, `pullMode`, `edgeMode`, debounce des entrées digitales

### Sémantique `activeHigh` / `edgeMode` pour les compteurs GPIO

Pour les entrées digitales en mode compteur, le driver ne raisonne pas directement en "front physique brut sur le pin".
Il convertit d'abord le niveau lu en un état logique `logicalOn`:

- `activeHigh=true`:
  `HIGH` = actif, `LOW` = inactif
- `activeHigh=false`:
  `LOW` = actif, `HIGH` = inactif

Le champ `edgeMode` est ensuite appliqué sur cette transition logique:

- `rising`: passage logique `inactif -> actif`
- `falling`: passage logique `actif -> inactif`
- `both`: les deux transitions logiques

Conséquence importante:
sur une entrée active bas (`activeHigh=false`), le front logique montant correspond physiquement à un front descendant sur le pin, et le front logique descendant correspond physiquement à un front montant sur le pin.

Tableau complet du comportement actuel du driver compteur GPIO:

| `activeHigh` | `edgeMode` | Interruption physique armée sur le pin | Transition logique effectivement comptée | Interprétation pratique |
|---|---|---|---|---|
| `true` | `falling` | front descendant | `actif -> inactif` | fin d'une impulsion active haut |
| `true` | `rising` | front montant | `inactif -> actif` | début d'une impulsion active haut |
| `true` | `both` | les deux fronts | les deux transitions | compte montée + descente |
| `false` | `falling` | front montant | `actif -> inactif` | fin d'une impulsion active bas |
| `false` | `rising` | front descendant | `inactif -> actif` | début d'une impulsion active bas |
| `false` | `both` | les deux fronts | les deux transitions | compte descente + remontée |

Exemples pratiques:

- capteur avec pull-up externe et impulsion active bas:
  `activeHigh=false`, `edgeMode=rising` comptera le début de l'impulsion, donc le front physique descendant
- capteur avec pull-up externe et impulsion active bas:
  `activeHigh=false`, `edgeMode=falling` comptera la fin de l'impulsion, donc le front physique montant
- capteur avec impulsion active haut:
  `activeHigh=true`, `edgeMode=rising` comptera le front physique montant

Cette convention est cohérente avec une interprétation "métier" de `edgeMode` dans le domaine logique du signal.
Elle peut toutefois surprendre si l'on s'attend à ce que `rising` et `falling` désignent toujours les fronts physiques bruts du GPIO.

## DataStore

Le module publie ses valeurs runtime via `Modules/IOModule/IORuntime.h`.

Écritures utilisées:

- `setIoEndpointFloat(...)`
- `setIoEndpointBool(...)`
- `setIoEndpointInt(...)`

Clés utilisées:

- base `DataKeys::IoBase`
- un index runtime par endpoint exposé

## MQTT runtime

`IOModule` implémente `IRuntimeSnapshotProvider`.

Routes publiées actuellement:

- `rt/io/input/aN`
- `rt/io/input/iN`
- `rt/io/output/dN`

Le module peut aussi construire des snapshots de groupe:

- entrées
- sorties

## Runtime UI

Le module expose actuellement des valeurs Runtime UI pour:

- température eau
- température air
- pH
- ORP
- compteur eau

Les identifiants sont déclarés dans `IOModule::RuntimeUiValueId`.

## Home Assistant

Le module IO n'enregistre pas seul toutes les entités Home Assistant. Dans le profil `FlowIO`, `src/Profiles/FlowIO/FlowIOIoAssembly.cpp` synchronise:

- les capteurs analogiques déclarés
- les binary sensors ou sensors des entrées digitales
- les switches liés aux sorties digitales présentes

Cette synchronisation repose sur:

- `HAModule`
- la table `PoolBinding`
- les suffixes runtime `rt/io/...`

## Particularités de fonctionnement

- les sorties peuvent être impulsionnelles (`momentary`)
- le masque PCF8574 est exposé via `StatusLedsService`
- les compteurs digitaux peuvent être persistés en NVS
- le module maintient `IoCycleInfo` pour exposer la liste des `IoId` modifiés sur le dernier cycle
- les labels exposés par `endpointLabel()` viennent des définitions construites par le profil
