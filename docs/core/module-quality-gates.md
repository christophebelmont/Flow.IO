# Quality Gates Modules

Cette page donne une note de qualité par module selon 10 critères homogènes.

## Méthode de notation

- Échelle: `0..10` par critère
- Note finale module: somme des 10 critères, donc `/100`
- Seuils:
  - `>= 85`: Conforme
  - `70..84`: À renforcer
  - `< 70`: Action prioritaire

## Les 10 points de Quality Gate

1. **Contrat d'interface**: API/service clair, stable, erreurs explicites.
2. **Validation des entrées**: contrôle des bornes/types, refus des payloads invalides.
3. **Persistance & migration**: cohérence NVS/ConfigStore, valeurs défaut sûres.
4. **Cohérence runtime**: DataStore/dirty flags/états sans ambiguïté.
5. **Robustesse temps réel**: comportement déterministe en boucle, non-blocage.
6. **Sécurité fonctionnelle**: fail-safe, interlocks, cut-off sur conditions dangereuses.
7. **Observabilité**: logs utiles, métriques exploitables, topics runtime lisibles.
8. **Gestion d'erreurs**: retour d'erreur exploitable, dégradation maîtrisée.
9. **Maintenabilité**: lisibilité, découplage, documentation module à jour.
10. **Empreinte ressources**: discipline RAM/stack/CPU adaptée ESP32.

## Notes par module

| Module | Note (/100) | Statut |
|---|---:|---|
| [LogHubModule](../modules/LogHubModule.md) | 88 | Conforme |
| [LogDispatcherModule](../modules/LogDispatcherModule.md) | 85 | Conforme |
| [LogSerialSinkModule](../modules/LogSerialSinkModule.md) | 83 | À renforcer |
| [EventBusModule](../modules/EventBusModule.md) | 84 | À renforcer |
| [ConfigStoreModule](../modules/ConfigStoreModule.md) | 91 | Conforme |
| [DataStoreModule](../modules/DataStoreModule.md) | 89 | Conforme |
| [CommandModule](../modules/CommandModule.md) | 86 | Conforme |
| [SystemModule](../modules/SystemModule.md) | 83 | À renforcer |
| [SystemMonitorModule](../modules/SystemMonitorModule.md) | 84 | À renforcer |
| [AlarmModule](../modules/AlarmModule.md) | 88 | Conforme |
| [WifiModule](../modules/WifiModule.md) | 81 | À renforcer |
| [TimeModule](../modules/TimeModule.md) | 89 | Conforme |
| [MQTTModule](../modules/MQTTModule.md) | 87 | Conforme |
| [HAModule](../modules/HAModule.md) | 85 | Conforme |
| [IOModule](../modules/IOModule.md) | 86 | Conforme |
| [PoolLogicModule](../modules/PoolLogicModule.md) | 89 | Conforme |
| [PoolDeviceModule](../modules/PoolDeviceModule.md) | 90 | Conforme |

## Interprétation rapide

- Les priorités de renforcement se concentrent sur les modules de transport/infra (`wifi`, `eventbus`, `sysmon`) en charge des aléas runtime.
- Les modules métier piscine (`poollogic`, `pooldev`) sont au niveau attendu, avec garde-fous runtime et persistance en place.
- Cette grille doit être revue à chaque release majeure et après changements d'architecture.
