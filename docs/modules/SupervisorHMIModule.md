# SupervisorHMIModule (`moduleId: hmi.supervisor`)

## Rôle

Interface HMI locale du firmware Supervisor:
- rendu statut réseau / lien Flow / firmware update sur TFT ST7789
- gestion rétroéclairage automatique avec capteur PIR
- reset WiFi local sur appui long bouton poussoir

## Dépendances

- `loghub`
- `config`
- `wifi`
- `wifiprov`
- `fwupdate`
- `i2ccfg.client`

## Comportement

- écran rafraîchi périodiquement sans blocage
- extinction backlight après timeout d'inactivité PIR
- rallumage du backlight sur détection de présence par le capteur PIR
- backlight forcé ON pendant update firmware
- appui long sur l'entrée `wifiResetPin` (3s par défaut dans le profil Supervisor): reset `wifi.ssid`/`wifi.pass`, notification provisioning, reboot

## Brochage et timings

Les valeurs viennent maintenant de la single source of truth Supervisor:

- hardware: `src/Board/SupervisorBoardRev1.h`
- timings runtime: `src/Profiles/Supervisor/SupervisorProfile.cpp`

Valeurs actuelles:

- TFT backlight: `14`
- TFT CS: `15`
- TFT DC: `2`
- TFT RST: `4`
- TFT MOSI: `23`
- TFT SCLK: `18`
- PIR: `36`
- bouton reset WiFi: `disabled`
- timeout extinction backlight: `10000 ms`
- appui long reset WiFi: `3000 ms`

Dans l'état actuel de `src/Board/SupervisorBoardRev1.h`, `wifiResetPin = -1`. Le comportement d'appui long est donc présent dans le module, mais aucun bouton matériel n'est défini par défaut sur cette révision de carte.
