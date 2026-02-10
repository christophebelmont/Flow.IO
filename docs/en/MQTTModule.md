# Flow.IO - MQTT Specification (v1.1)

Version: **1.1**

This version keeps a single command topic and a single ACK topic, plus runtime snapshot publication.

## 1) Root Topic

All MQTT topics are published under:

```text
flowio/<deviceId>/
```

Example:

```text
flowio/ESP32-670D34/
```

`deviceId` must be unique and stable.

## 2) Namespaces

| Namespace | Role | Writable | Persistent (NVS) | Producer |
|---|---|---:|---:|---|
| `cfg` | Configuration | Yes | Usually yes | UI/MQTT |
| `rt/*` | Runtime telemetry/state | No | No | Firmware |
| `cmd` | Commands | Yes | No | UI/MQTT |
| `evt` | Events | No | No | Firmware |

## 3) QoS and Retain Recommendations

| Topic family | QoS | retain |
|---|---:|---:|
| `status` | 1 | Yes |
| `cfg/*` | 1 | Yes |
| `rt/*` | 0 (or 1) | Yes (recommended) |
| `cmd` | 0/1 | No |
| `ack` | 0/1 | No |
| `evt` | 1 | No |

## 4) Presence

Topic:

```text
flowio/<deviceId>/status
```

Online payload (retained):

```json
{"online":true}
```

Offline payload (LWT retained):

```json
{"online":false}
```

## 5) Commands

Topic:

```text
flowio/<deviceId>/cmd
```

Payload:

```json
{"cmd":"system.ping","args":{}}
```

ACK topic:

```text
flowio/<deviceId>/ack
```

ACK payload:

```json
{"ok":true,"cmd":"system.ping","reply":{"ok":true,"pong":true}}
```

## 6) Configuration

### 6.1 Module snapshots

Topic format:

```text
flowio/<deviceId>/cfg/<module>
```

Examples:

```text
flowio/<deviceId>/cfg/mqtt
flowio/<deviceId>/cfg/io
```

### 6.2 Apply configuration

Topic:

```text
flowio/<deviceId>/cfg/set
```

Payload format is flat: `module -> object`.

Valid:

```json
{"mqtt":{"baseTopic":"flowio2"}}
```

Valid IO example:

```json
{
  "io/output/d0": {
    "d0_active_high": false
  }
}
```

ACK topic:

```text
flowio/<deviceId>/cfg/ack
```

## 7) Runtime Snapshots

### 7.1 IO

Topics:

```text
flowio/<deviceId>/rt/io/input/state
flowio/<deviceId>/rt/io/output/state
```

Input payload example:

```json
{
  "a0": {"name":"ph","value":7.12},
  "a1": {"name":"orp","value":732.0},
  "a2": {"name":"psi","value":1.8},
  "a3": {"name":"water_temp","value":26.4},
  "a4": {"name":"air_temp","value":24.1},
  "ts": 12345678
}
```

Output payload example:

```json
{
  "d0": {"name":"Filtration Pump","value":false},
  "d3": {"name":"Chlorine Generator","value":true},
  "status_leds_mask": {"name":"status_leds_mask","value":128},
  "ts": 12345678
}
```

Change behavior:

- publication is triggered by `DataStore` snapshot events
- `DIRTY_SENSORS` triggers input block publication
- `DIRTY_ACTUATORS` triggers output block publication
- only changed block is republished
- `sensor_min_publish_ms` applies to input block only

### 7.2 Network

Topic:

```text
flowio/<deviceId>/rt/network/state
```

Payload:

```json
{
  "ready": true,
  "ip": "192.168.86.28",
  "rssi": -60,
  "mqtt": true,
  "ts": 12345678
}
```

### 7.3 System

Topic:

```text
flowio/<deviceId>/rt/system/state
```

Payload:

```json
{
  "upt_ms": 12345678,
  "heap": {"free": 180000, "min": 160000, "largest": 90000, "frag": 15},
  "ts": 12345678
}
```

## 8) Default Cadence

- `rt/io/input/state`: on change, throttled by `sensor_min_publish_ms`
- `rt/io/output/state`: immediate on change
- `rt/network/state`: every 60s
- `rt/system/state`: every 60s

Cadence is configurable through `cfg/io` and `cfg/mqtt`.
