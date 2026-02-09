# Sensors — Configuration et fonctionnement

Ce document décrit comment définir les capteurs dans Flow.io, les paramètres de calibration, la précision, et la chaîne de mesure.

---

## 1) Capteurs supportés

- pH
- ORP
- PSI (pression)
- Température eau
- Température air

---

## 2) Chaîne de mesure

1. Lecture brute ADS1115 (single ou différentiel)
2. Conversion en mV
3. Filtrage (RunningMedian 11 / moyenne des 5 centrales)
4. Calibration : `val = C0 * mesure + C1`
5. Arrondi selon précision (0/1/2 décimales)
6. Écriture DataStore + events

---

## 3) Configuration I2C et ADS1115

### 3.1 Bus I2C

| Clé | Type | Description | Défaut |
|---|---|---|---|
| `i2c_sda` | int | Pin SDA | 21 |
| `i2c_scl` | int | Pin SCL | 22 |

### 3.2 ADS1115

| Clé | Type | Description | Défaut |
|---|---|---|---|
| `ads_addr` | uint8 | Adresse ADS primaire (PSI) | 0x48 |
| `ads_addr2` | uint8 | Adresse ADS secondaire (pH/ORP si `adc_mode=1`) | 0x49 |
| `ads_gain` | int | Gain ADS1115 (lib RobTillaart) | 6144mV |
| `ads_rate` | int | SPS (0..7, 1=16sps) | 1 |
| `ads_min` | float | Valeur min brute | -32768 |
| `ads_max` | float | Valeur max brute | 32767 |

### 3.3 Mode pH/ORP

| Clé | Type | Description | Valeurs |
|---|---|---|---|
| `adc_mode` | int | 0 = pH/ORP sur ADS primaire, 1 = pH/ORP sur ADS secondaire | 0/1 |

- PSI est **toujours** sur ADS primaire.
- En mode `adc_mode=1`, pH/ORP sont lus en **différentiel** (0‑1 / 2‑3).

---

## 4) Calibration (C0/C1)

Formule :

```
val = C0 * mesure + C1
```

### Clés disponibles

| Capteur | C0 | C1 |
|---|---|---|
| pH | `ph_c0` | `ph_c1` |
| ORP | `orp_c0` | `orp_c1` |
| PSI | `psi_c0` | `psi_c1` |
| Eau | `water_c0` | `water_c1` |
| Air | `air_c0` | `air_c1` |

Par défaut : `C0=1`, `C1=0`.

---

## 5) Précision (arrondi)

Le runtime est arrondi **avant** d’être écrit dans le DataStore, ce qui évite des events inutiles si la valeur n’a pas changé à la précision choisie.

| Capteur | Clé | Défaut |
|---|---|---|
| pH | `ph_prec` | 1 |
| ORP | `orp_prec` | 0 |
| PSI | `psi_prec` | 1 |
| Eau | `water_prec` | 1 |
| Air | `air_prec` | 1 |

Valeurs :
- `0` = entier
- `1` = 1 décimale
- `2` = 2 décimales

---

## 6) OneWire (températures)

| Clé | Type | Description | Défaut |
|---|---|---|---|
| `onewire_water_pin` | int | Pin OneWire eau | 19 |
| `onewire_air_pin` | int | Pin OneWire air | 18 |

---

## 7) Runtime DataStore

Les valeurs runtime sont publiées dans :
- `rt.sensors.ph`
- `rt.sensors.orp`
- `rt.sensors.psi`
- `rt.sensors.waterTemp`
- `rt.sensors.airTemp`

Les snapshots MQTT sont envoyés toutes les 15 secondes sur :
```
flowio/<deviceId>/rt/sensors/state
```
