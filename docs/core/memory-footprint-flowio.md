# Empreinte memoire Flow.io

Cette note documente l'occupation memoire du firmware `FlowIO` sur ESP32, avec deux angles:

- DRAM statique au link (`.dram0.data` + `.dram0.bss`)
- heap persistante estimee apres boot (allocations conservees pendant toute la vie du firmware)

Les chiffres ci-dessous correspondent a la build locale `FlowIO` generee depuis:

- ELF: `.pio/build/FlowIO/firmware.elf`
- map: `.pio/build/FlowIO/firmware.map`

## Totaux globaux

Sortie `xtensa-esp32-elf-size -A .pio/build/FlowIO/firmware.elf`:

| Section | Taille |
|---|---:|
| `.dram0.data` | `25 936 o` |
| `.dram0.bss` | `95 712 o` |
| Total DRAM statique | `121 648 o` |

Ce total correspond au `RAM: used 121648 bytes` reporte par PlatformIO.

## Methode

Il faut distinguer trois niveaux:

1. Le `.map` donne la verite du link pour les symboles statiques visibles.
2. La structure `Profiles::FlowIO::ModuleInstances` est un unique gros symbole en `.bss`, donc le `.map` ne permet pas de decomposer directement son contenu interne par module.
3. La decomposition fine "par module" et "par structure interne" utilise donc un mix:
   - `firmware.map` / `nm` pour les gros symboles globaux
   - `sizeof(...)` et tailles de tableaux pour les structures internes agregees dans `ModuleInstances`
   - lecture du code pour les allocations heap persistantes (`heap_caps_*`, `new (std::nothrow)`)

En consequence:

- les chiffres "DRAM statique" issus du `.map` sont precis
- les chiffres "par module" et "par structure interne" sont des estimations robustes, utiles pour prioriser les optimisations
- les allocations transitoires (`malloc()` ponctuel, `DynamicJsonDocument` local, buffers temporaires HMI/Web) ne sont pas comptabilisées dans la heap persistante ci-dessous

## Vue par module

Le bloc principal du profil `FlowIO` est:

- `Profiles::FlowIO::moduleInstances()::instances`: `57 424 o` en `.bss`

Ce bloc contient presque tous les objets module embarques par valeur.

### Modules dominants

Les tailles ci-dessous representent l'empreinte statique de l'instance module embarquee dans `ModuleInstances`, puis la heap persistante directement attributable au module.

| Module / bloc | DRAM statique estimee | Heap persistante estimee | Notes |
|---|---:|---:|---|
| `IOModule` | `~11 500 o` | `0 o` | Gros pools internes statiques, notamment 17 slots analogiques, pas de heap persistante dediee |
| `MQTTModule` | `9 416 o` | `~6 970 o` | `scratch_` (`2 630 o`) + `rxQueueStorage_` (`4 096 o`) + `cfgProducer_` (`~244 o`) |
| `PoolDeviceModule` | `6 248 o` | `~676 o` | `cfgRoutes_` (`~432 o`) + `cfgMqttPub_` (`~244 o`) |
| `TimeModule` | `4 704 o` | `~244 o` | `cfgMqttPub_` |
| `LogHubModule` | `4 316 o` | `~244 o` | `cfgMqttPub_` |
| `I2CCfgServerModule` | `4 220 o` | `~244 o` | `cfgMqttPub_`; module passif avec task conditionnelle |
| `HAModule` | `4 128 o` | `~244 o` | `cfgMqttPub_`; la heap de discovery HA du profil est listee plus bas, hors module |
| `AlarmModule` | `2 460 o` | `~244 o` | `cfgMqttPub_` |
| `PoolLogicModule` | `1 992 o` | `~244 o` | `cfgMqttPub_` |
| `WifiModule` | `1 340 o` | `~244 o` | `cfgMqttPub_` |
| `HMIModule` | `1 108 o` | `0 o` | Pas d'allocation persistante specifique identifiee |
| `DataStoreModule` | `1 040 o` | `0 o` | Module passif |
| `EventBusModule` | `880 o` | `0 o` | La queue d'event est embarquee dans la structure module |
| `Autres modules + champs profil` | `~808 o` | `0 o` | `CommandModule`, `ConfigStoreModule`, `SystemModule`, sinks logs, `OneWireBus`, topics profil, etat boot |

Notes:

- `MQTTModule` et `IOModule` sont les deux plus gros postes statiques au niveau module.
- Le buffer `MQTTModule::scratch_` est aujourd'hui alloue en heap persistante et ne figure plus dans ce poste `.bss`.
- La heap "profil FlowIO" liee a la discovery Home Assistant n'est pas dans `HAModule`, mais dans l'assemblage du profil.

### Heap runtime recurrente: stacks de tasks

Une part importante de la heap runtime provient aussi des stacks de tasks declarees par les modules actifs. Les valeurs ci-dessous sont les tailles de stack configurees par les modules du profil `FlowIO`:

| Module actif | Stack declaree |
|---|---:|
| `EventBusModule` | `2 816 o` |
| `HMIModule` | `4 096 o` |
| `AlarmModule` | `2 816 o` |
| `WifiModule` | `2 816 o` |
| `TimeModule` | `2 816 o` |
| `MQTTModule` | `5 712 o` |
| `HAModule` | `3 072 o` |
| `IOModule` | `2 560 o` |
| `PoolLogicModule` | `3 072 o` (stack par defaut) |
| `PoolDeviceModule` | `3 072 o` (stack par defaut) |
| `SystemMonitorModule` | `3 584 o` |

Sous-total toujours actif dans `FlowIO`: environ `36,4 Ko` de stacks configurees, hors task conditionnelle de `I2CCfgServerModule`.

## Vue par symbole / structure

### Gros symboles statiques visibles dans le `.map`

Les tailles ci-dessous proviennent directement de `firmware.map` ou `xtensa-esp32-elf-nm`.

| Symbole | Type | Taille | Source |
|---|---|---:|---|
| `Profiles::FlowIO::moduleInstances()::instances` | `.bss` | `57 424 o` | Profil `FlowIO` |
| `gContext` | `.bss` | `10 060 o` | `src/App/Bootstrap.cpp` |
| `ConfigStore::applyJson()::doc` | `.bss` | `1 064 o` | `src/Core/ConfigStore.cpp` |
| `MQTTModule::processRxCmd_()::doc` | `.bss` | `1 064 o` | `src/Modules/Network/MQTTModule/MQTTRx.cpp` |
| `MQTTModule::processRxCfgSet_()::cfgDoc` | `.bss` | `1 064 o` | `src/Modules/Network/MQTTModule/MQTTRx.cpp` |
| `parseCmdArgsObject_()` de `TimeModule` | `.bss` | `808 o` | `src/Modules/Network/TimeModule/TimeModule.cpp` |
| `parseCmdArgsObject_()` de `AlarmModule` | `.bss` | `296 o` | `src/Modules/AlarmModule/AlarmModule.cpp` |
| `parseCmdArgsObject_()` de `PoolDeviceCommands` | `.bss` | `296 o` | `src/Modules/PoolDeviceModule/PoolDeviceCommands.cpp` |
| `parseCmdArgsObject_()` de `PoolLogicCommands` | `.bss` | `296 o` | `src/Modules/PoolLogicModule/PoolLogicCommands.cpp` |
| `gSwitchStateSuffix` | `.bss` | `192 o` | `src/Profiles/FlowIO/FlowIOIoAssembly.cpp` |
| `gAnalogStateSuffix` | `.bss` | `144 o` | `src/Profiles/FlowIO/FlowIOIoAssembly.cpp` |
| `gDigitalStateSuffix` | `.bss` | `72 o` | `src/Profiles/FlowIO/FlowIOIoAssembly.cpp` |

Observation:

- `ModuleInstances` + `gContext` representent a eux seuls `67 484 o` de DRAM statique.
- Les `StaticJsonDocument<>` persistants pèsent encore plusieurs kilo-octets en `.bss` hors modules.

### Structures internes majeures cachees dans `ModuleInstances`

Ces tailles ne sont pas directement visibles dans le `.map`, car elles sont incluses dans le gros symbole `moduleInstances()::instances`.

| Structure / tableau | Type | Taille estimee | Remarque |
|---|---|---:|---|
| `MQTTModule::ackMessages_[2]` | statique | `~2 092 o` | 2 messages ACK avec payload de reponse |
| `MQTTModule::highQ_ + normalQ_ + lowQ_` | statique | `~1 858 o` | anneaux de jobs |
| `MQTTModule::jobs_[80]` | statique | `~1 600 o` | slots de jobs |
| `TimeModule::scheduleBlob_` | statique | `1 536 o` | blob persistant scheduler |
| `TimeModule::schedulePersistBuf_` | statique | `1 536 o` | buffer scratch de serialisation |
| `PoolDeviceModule::runtimePersistBuf_[8][192]` | statique | `1 536 o` | staging NVS runtime devices |
| `PoolDeviceModule::slots_[8]` | statique | `~1 280 o` | etat complet des 8 appareils |
| `IOModule::digitalSlots_[15]` | statique | `~2 100 o` | 15 slots digitaux |
| `IOModule::analogSlots_[17]` | statique | `~1 700 o` | 17 slots analogiques |
| `TimeModule::sched_[16]` | statique | `~1 152 o` | runtime scheduler |

Ces postes sont utiles pour cibler les reductions reelles de `.bss` quand on ne veut pas tout basculer en heap.

### Allocations heap persistantes identifiees

Les allocations ci-dessous sont faites au boot ou au premier usage, puis ne sont pas liberees pendant la vie normale du firmware.

| Allocation | Heap estimee | Remarque |
|---|---:|---|
| `MQTTModule::rxQueueStorage_` | `4 096 o` | `8 * sizeof(RxMsg)` |
| `FlowIoDiscoveryHeap` | `2 816 o` | templates HA et payloads switches du profil `FlowIO` |
| `MQTTModule::scratch_` | `2 630 o` | buffers `topic/payload/reply` |
| `10 x MqttConfigRouteProducer` | `~2 440 o` | environ `244 o` chacun: `Alarm`, `LogHub`, `PoolLogic`, `I2CCfgServer`, `PoolDevice`, `SystemMonitor`, `Wifi`, `MQTT`, `Time`, `HA` |
| `PoolDeviceModule::cfgRoutes_` | `~432 o` | `18 routes * ~24 o` |

Sous-total heap persistante identifiable hors stacks: environ `12,4 Ko`.

Remarques:

- des buffers auparavant portes par la DRAM statique sont aujourd'hui comptes dans la heap persistante identifiee ci-dessus:
  - `FlowIoDiscoveryHeap`: `2 816 o`
  - `MQTTModule::scratch_`: `2 630 o`
- `IOModule` utilise beaucoup de pools internes, mais ils restent statiques dans `ModuleInstances` et ne consomment donc pas de heap.

## Postes dominants pour le dimensionnement

Les sections precedentes montrent surtout:

1. que `IOModule`, `MQTTModule`, `PoolDeviceModule` et `TimeModule` dominent l'empreinte du profil `FlowIO`
2. que `ModuleInstances` et `gContext` concentrent une part importante de la DRAM statique
3. que les stacks de tâches et quelques buffers réseau représentent la majeure partie de la heap persistante identifiable

## Commandes utiles pour regenerer la note

Build:

```sh
~/.platformio/penv/bin/pio run -e FlowIO
```

Vue sections ELF:

```sh
~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-size -A .pio/build/FlowIO/firmware.elf
```

Top symboles de donnees:

```sh
~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm -S --size-sort .pio/build/FlowIO/firmware.elf | rg ' [bBdD] '
```

Map:

```sh
sed -n '37888,37966p' .pio/build/FlowIO/firmware.map
```
