# Flow.IO

Flow.IO est une plateforme de pilotage piscine connectée orientée fiabilité: elle automatise la qualité d'eau, réduit les opérations manuelles, et donne une supervision claire des équipements en local comme à distance.

![Home Automation Integration](docs/pictures/Grafana%20and%20App.png)

## Pourquoi Flow.IO

Sans orchestration continue, on observe vite:
- dérive pH / ORP
- filtration mal dimensionnée par rapport à la température
- surconsommation de produits et d'énergie
- usure prématurée des pompes et actionneurs

Flow.IO apporte un pilotage cohérent de bout en bout.

![PoolMaster Ecosystem](docs/pictures/PoolMaster%20Ecosystem.png)

## Mesure et contrôle en continu

Mesures suivies en continu:
- température d'eau et d'air
- pression de filtration
- pH
- ORP
- niveau bassin
- métriques de fonctionnement des équipements (temps de marche, volumes injectés estimés, niveau cuves estimé)

Actionneurs pilotés:
- pompe de filtration
- pompes péristaltiques pH / chlore liquide
- électrolyse (SWG)
- pompe robot
- pompe de remplissage
- relais auxiliaires (ex: éclairage, chauffage, équipements externes)

## Interface locale tactile

L'interface Nextion offre une vue synthétique des mesures, états et commandes principales pour l'exploitation quotidienne au bord du bassin.

![Nextion TouchScreen HMI](docs/pictures/Nextion_Screens_v5.jpg)

## Automatisation utile au quotidien

- calcul automatique de la fenêtre de filtration selon la température d'eau
- priorisation sécurité/actionneurs avec interlocks de dépendance
- gestion temporelle (jour/semaine/mois) persistante, y compris après reboot
- modes d'exploitation (auto, manuel, hiver, protection gel)
- supervision alarmes (pression, états critiques)

## Principe de régulation PID (pH / ORP)

Flow.IO implémente une régulation PID temporelle dans `PoolLogicModule` pour les pompes péristaltiques pH et ORP:
- calcul PID périodique (par défaut toutes les `30 s`)
- conversion de la sortie en durée d'activation `output_on_ms` bornée dans une fenêtre fixe (`window_ms`, typiquement `1 h`)
- commande ON/OFF dans la fenêtre: la pompe est active en début de fenêtre pendant `output_on_ms`

Les paramètres (`setpoint`, `Kp/Ki/Kd`, `window_ms`, `pid_sample_ms`, `pid_min_on_ms`) sont persistants via `ConfigStore`/NVS.  
Si les conditions de sécurité ne sont pas réunies (filtration arrêtée, mode hiver, capteur indisponible, défaut pression, etc.), la sortie est remise à `0` et la pompe est coupée.

Détail complet de l'algorithme, des conditions d'activation et des topics runtime dans la documentation module:
- [PoolLogicModule](docs/modules/PoolLogicModule.md)

## Intégration et exploitation

- publication MQTT structurée (`cfg/*`, `rt/*`, `cmd`, `ack`)
- auto-discovery Home Assistant
- intégration possible avec Jeedom/Node-RED/InfluxDB/Grafana via MQTT
- architecture modulaire robuste (FreeRTOS + services Core + EventBus + DataStore + ConfigStore/NVS)

Résultat: une eau plus stable, une maintenance plus prévisible et une meilleure maîtrise des coûts d'exploitation.

![Grafana](docs/pictures/Grafana.png)

## Documentation développeur

La documentation complète (architecture, services Core, flux EventBus/DataStore/MQTT, et fiche détaillée par module) est disponible ici:

- [Documentation complète](docs/README.md)

## Référence rapide

- Firmware: ESP32 + FreeRTOS
- Configuration persistante: `ConfigStore` sur NVS (`Preferences`)
- Runtime partagé: `DataStore`
- Bus d'événements: `EventBus`
- Transport externe: MQTT
- Intégration domotique: Home Assistant discovery
