# Protocole ESP / Nextion (FlowIO)

## Objet

Ce document décrit le contrat actuel entre `FlowIO` et un écran Nextion piloté
par `HMIModule` / `NextionDriver`.

Le protocole est volontairement mixte :
- `ESP -> Nextion` : commandes Nextion natives (`.txt`, `.val`, `page ...`)
- `Nextion -> ESP` : protocole binaire compact `# <len> <opcode> <payload...>`
- compatibilité conservée : événements touch Nextion (`0x65`) et événements
  texte `EV:*` terminés par `FF FF FF`

Le port série utilisé côté FlowIO est `Serial2` sur `RX16 / TX17` à `115200`.

## Principes

- Les valeurs métier affichées (`pH`, `ORP`, températures, heure, date) sont
  formatées côté ESP puis envoyées en `.txt`.
- Les états discrets et indicateurs compacts sont envoyés en `.val`.
- L'écran Nextion n'est pas la source de vérité : il émet des intentions ou des
  commandes UI, puis `FlowIO` rafraîchit l'affichage à partir du `DataStore`,
  du `ConfigStore` et des états runtime.
- Les objets Nextion utilisés par l'ESP doivent être considérés comme faisant
  partie du contrat de firmware HMI.

## Objets Nextion pilotés par l'ESP

### Page Home

Objets texte :
- `tWaterTemp.txt`
- `tAirTemp.txt`
- `tpH.txt`
- `tORP.txt`

Objets numériques :
- `vapHPercent.val`
- `vaOrpPercent.val`
- `globals.vaStates.val`

Sémantique :
- `tWaterTemp.txt` : exemple `27.4 C`
- `tAirTemp.txt` : exemple `21.8 C`
- `tpH.txt` : exemple `7.18`
- `tORP.txt` : exemple `650 mV`
- `vapHPercent.val` : jauge `0..180`, `90` à la consigne pH
- `vaOrpPercent.val` : jauge `0..180`, `90` à la consigne ORP
- `globals.vaStates.val` : bitmap d'état compact

### Bitmap `globals.vaStates`

Bits actuellement utilisés :
- bit `0` : filtration ON
- bit `1` : pompe pH ON
- bit `2` : pompe ORP ON
- bit `3` : mode auto global
- bit `4` : mode auto pH
- bit `5` : mode auto ORP
- bit `6` : mode hiver
- bit `7` : WiFi connecté
- bit `8` : MQTT connecté
- bit `9` : niveau eau bas
- bit `10` : bidon pH bas
- bit `11` : bidon chlore bas
- bit `12` : alarme temps max pompe pH
- bit `13` : alarme temps max pompe ORP
- bit `14` : alarme PSI

### Page de configuration

Quand le menu config est compilé, le driver rend sur la page `pageCfgMenu`
avec les objets suivants :

- `tPath`
- `bHome`
- `bBack`
- `bValid`
- `bPrev`
- `bNext`
- `nPage`
- `nPages`
- `tL0` .. `tL5`
- `tV0` .. `tV5`
- `bR0` .. `bR5`

Sémantique :
- `tPath` : breadcrumb courant
- `tLx` : libellé de ligne
- `tVx` : valeur de ligne
- `bRx` : zone tactile de ligne

## Protocole Nextion -> ESP

## Format binaire

Format :

```text
0x23 <len> <opcode> <payload...>
```

Où :
- `0x23` est le caractère `#`
- `len` est le nombre d'octets qui suivent
- `opcode` est le type de message
- `payload` dépend de l'opcode

Exemple :

```text
23 03 60 01 01
```

Décodage :
- `23` : début de trame
- `03` : trois octets suivent
- `60` : opcode `HOME_ACTION`
- `01` : action `FILTRATION_SET`
- `01` : valeur `true`

## Opcodes supportés

### `0x50` : `PAGE`

Payload :
- `page_id` (`uint8`)

Usage :
- utilisé par Nextion pour informer l'ESP de la page actuellement affichée
- permet au driver de savoir si la page config est déjà visible

Exemple Nextion :

```text
printh 23 02 50 0A
```

Exemple :
- `0A` = page config

### `0x51` : `NAV`

Payload :
- `nav_id` (`uint8`)

Valeurs supportées :
- `0x01` : `HOME`
- `0x02` : `BACK`
- `0x03` : `VALIDATE`
- `0x04` : `NEXT_PAGE`
- `0x05` : `PREV_PAGE`

Exemple Nextion :

```text
printh 23 02 51 04
```

### `0x52` : `ROW_ACTIVATE`

Payload :
- `row` (`uint8`, `0..5`)

Exemple :

```text
printh 23 02 52 03
```

Active la ligne `3`.

### `0x53` : `ROW_TOGGLE`

Payload :
- `row` (`uint8`, `0..5`)

Exemple :

```text
printh 23 02 53 01
```

Toggle la ligne `1`.

### `0x54` : `ROW_CYCLE`

Payload :
- `row` (`uint8`, `0..5`)
- `dir` (`uint8`)

Valeurs `dir` :
- `0x00` ou `0xFF` : précédent
- toute autre valeur : suivant

Exemple :

```text
printh 23 03 54 02 01
```

Fait avancer la ligne `2`.

### `0x60` : `HOME_ACTION`

Payload :
- `action_id` (`uint8`)
- `value` (`uint8`)

Actions supportées :
- `0x01` : `FILTRATION_SET`
- `0x02` : `AUTO_MODE_SET`
- `0x03` : `SYNC_REQUEST`

Valeur :
- `0x00` : faux / OFF
- `0x01` : vrai / ON

Exemples :

Filtration ON :

```text
printh 23 03 60 01 01
```

Filtration OFF :

```text
printh 23 03 60 01 00
```

Mode auto ON :

```text
printh 23 03 60 02 01
```

Demande de resynchronisation Home :

```text
printh 23 03 60 03 01
```

## Mapping vers les commandes ESP

Les actions Home sont routées via `CommandService` :

- `FILTRATION_SET`
  - commande : `poollogic.filtration.write`
  - payload envoyé par l'ESP :

```json
{"value":true}
```

- `AUTO_MODE_SET`
  - commande : `poollogic.auto_mode.set`
  - payload envoyé par l'ESP :

```json
{"value":false}
```

- `SYNC_REQUEST`
  - ne passe pas par `CommandService`
  - demande simplement à `HMIModule` de republier toutes les données Home

Il n'y a pas d'ACK synchrone formel vers Nextion dans cette V1.
Le retour attendu est le rafraîchissement de l'état réel après exécution.

## Compatibilité conservée

En plus du protocole binaire ci-dessus, `NextionDriver` accepte encore :

- événements touch Nextion standard `0x65`
- événements texte `EV:*` terminés par `FF FF FF`

Exemples historiques acceptés :
- `EV:HOME`
- `EV:BACK`
- `EV:VAL`
- `EV:NEXT`
- `EV:PREV`
- `EV:ROW:2`
- `EV:TOG:1`
- `EV:CYC:3:1`

Cela permet une migration progressive du firmware Nextion.

## Pages et identifiants recommandés

Identifiants recommandés côté Nextion :
- `0x00` : page Home
- `0x0A` : page `pageCfgMenu`

L'opcode `PAGE` peut être émis dans le `Preinitialize Event` de chaque page.

Exemples :

Home :

```text
printh 23 02 50 00
```

Config :

```text
printh 23 02 50 0A
```

## Intégration Nextion recommandée

### Home

Les widgets Home peuvent rester entièrement locaux pour le rendu, mais les
boutons d'action doivent envoyer des trames binaires.

Exemples :
- bouton filtration ON : `printh 23 03 60 01 01`
- bouton filtration OFF : `printh 23 03 60 01 00`
- bouton auto ON : `printh 23 03 60 02 01`
- bouton sync : `printh 23 03 60 03 01`

### Config

La page config peut s'appuyer sur :
- `NAV`
- `ROW_ACTIVATE`
- `ROW_TOGGLE`
- `ROW_CYCLE`

Exemples :
- `bHome` : `printh 23 02 51 01`
- `bBack` : `printh 23 02 51 02`
- `bValid` : `printh 23 02 51 03`
- `bNext` : `printh 23 02 51 04`
- `bPrev` : `printh 23 02 51 05`
- `bR0` : `printh 23 02 52 00`
- `bR1` : `printh 23 02 52 01`

## Limitations actuelles

- Pas de handshake de version d'affichage encore implémenté.
- Pas d'ACK binaire structuré renvoyé vers Nextion.
- Les éditions texte/slider complexes peuvent continuer à utiliser le fallback
  `EV:*` tant qu'un framing binaire dédié n'est pas nécessaire.
- `globals.vaStates` est un contrat de firmware : toute modification d'ordre de
  bits doit être synchronisée entre ESP et Nextion.
