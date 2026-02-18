# HMIModule (`moduleId: hmi`)

## Rôle

Couche d'orchestration HMI locale:
- lit/édite la configuration via `ConfigStoreService`
- maintient un menu de configuration paginé (6 lignes/page)
- délègue le rendu et les entrées au driver matériel (`IHmiDriver`)

En V1, le driver embarqué est `NextionDriver`.

## Dépendances

- `loghub`
- `config`
- `eventbus`

## Service exposé

- `hmi` -> `HmiService`
  - `requestRefresh()`
  - `openConfigHome()`
  - `openConfigModule(module)`
  - `buildConfigMenuJson(out)`

## Fonctionnement menu config

- Racine: liste des modules `cfg/*` via `listModules()`
- Détail module: clés/valeurs via `toJsonModule()`
- Pagination: 6 lignes
- Actions UI: `Home`, `Back`, `Valider`, `Prev`, `Next`
- `Valider`: patch JSON ciblé via `applyJson()`

Le chemin courant est exposé sous forme breadcrumb:
`flow > cfg > <module>`

## Typage des champs

Types de widget supportés:
- `Text`
- `Switch`
- `Select`
- `Slider`

Par défaut, le type est déduit depuis JSON (bool/int/float/texte).
Des hints peuvent forcer le widget et les contraintes (bornes/options).

## Driver Nextion (V1)

- Transport: mappé via `include/Board/BoardSerialMap.h`
  - mode normal: logs -> `Serial`, Nextion -> `Serial2` (RX16/TX17)
  - inversion: définir `FLOW_SWAP_LOG_HMI_SERIAL=1` au build pour
    logs -> `Serial2` (RX16/TX17), Nextion -> `Serial`
  - note: en mode inversé, UART0 reste utilisé par les messages ROM boot ESP32
- Rendu page config sur objets Nextion conventionnels:
  - `tPath`, `tL0..tL5`, `tV0..tV5`
  - `bHome`, `bBack`, `bValid`, `bPrev`, `bNext`, `bR0..bR5`
  - `nPage`, `nPages`
- Entrées:
  - événements touch Nextion standard (`0x65`)
  - événements texte optionnels (`EV:*`) terminés par `FF FF FF`

## Événements internes

Sur `EventId::ConfigChanged`, si le module affiché est impacté, l'écran est resynchronisé automatiquement.
