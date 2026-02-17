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

Dans ce projet, la bibliothèque Arduino PID est utilisée pour démarrer/arrêter les pompes de produits chimiques de manière cyclique, comme un signal de type PWM temporel. La période de cycle est fixée par le paramètre `WINDOW SIZE` (en millisecondes) et le PID fait varier le rapport cyclique dans cette fenêtre.

Si l'erreur calculée par la boucle PID est nulle ou négative, la sortie est ramenée à 0 et la pompe n'est pas activée. Si l'erreur est positive, la sortie prend une valeur entre 0 et la `WINDOW SIZE`, jusqu'au fonctionnement continu à 100%.

En pratique, la sortie de `PID.Compute()` est donc une durée d'activation (en ms) à appliquer à chaque cycle. Par exemple, avec une `WINDOW SIZE = 3600000 ms` (1 heure) et une sortie PID de `600000 ms` (10 minutes), la pompe fonctionne 10 minutes au début de chaque cycle d'une heure.

Concernant les gains par défaut, `Ki` et `Kd` peuvent être laissés à 0 pour privilégier la stabilité, ce qui revient à une régulation proportionnelle pure (boucle P). Ajouter du `Ki`/`Kd` peut améliorer la performance théorique, mais augmente la complexité de réglage et le risque d'instabilité.

Le choix de `Kp` se fait expérimentalement à partir de la réponse réelle du bassin. Exemple: si `83 ml` d'acide font varier le pH de `0,1` et que le débit de pompe est `1,5 L/h`, on obtient environ `3,3 min` d'injection pour corriger `0,1` pH, soit ~`200000 ms`. Pour une erreur de `1,0`, on peut en déduire un `Kp` de l'ordre de `2000000` pour la boucle pH (même logique pour la boucle ORP).

Le choix de `WINDOW SIZE` dépend du temps de réaction chimique du bassin. Si l'effet d'une injection met jusqu'à ~30 minutes à se stabiliser, la fenêtre doit être d'au moins 30 minutes (souvent plus) pour éviter le surdosage par corrections trop rapprochées. Une fenêtre d'1 heure (`3600000 ms`) constitue généralement un réglage prudent.

## Intégration et exploitation

- publication MQTT structurée (`cfg/*`, `rt/*`, `cmd`, `ack`)
- auto-discovery Home Assistant
- intégration possible avec Jeedom/Node-RED/InfluxDB/Grafana via MQTT
- architecture modulaire robuste (FreeRTOS + services Core + EventBus + DataStore + ConfigStore/NVS)

![Phone Interface](docs/pictures/Phone%20Interface.jpg)

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
