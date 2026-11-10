---
description: "RoofRoof's MQTT topics: availability, alerts and the alerts pause switch, with Home Assistant discovery. Roof topics come in a later spec."
---

# MQTT Integration

RoofRoof publishes its availability and alerts to any MQTT broker, accepts a pause/resume command for alerts, and can announce itself to Home Assistant automatically.

!!! note "Roof topics come in a later spec"
    This release is the platform only. The roof's state, position and commands *come in a later spec* and will be added under the same base topic.

---

## Enabling MQTT

In **Settings → Network → MQTT**, turn on **Publish to a broker** and set the broker, credentials and **Base topic** (default `roofroof`). The same settings over the API:

```json
{
  "mqtt": {
    "enabled": true,
    "broker": "192.168.1.10",
    "port": 1883,
    "username": "mqttuser",
    "password": "mqttpass",
    "topic": "roofroof",
    "homeAssistant": { "enabled": true, "discoveryPrefix": "homeassistant" }
  }
}
```

The base topic may use letters, numbers, `_` and `-`, with `/` between levels. `publishIntervalMs` (default 60000) is the publish interval for periodic documents; nothing is published periodically yet.

!!! warning "Telemetry, not an interlock"
    MQTT is a plaintext LAN integration. Use it for telemetry and automation; keep physical safety interlocks independent.

---

## Topics

| Topic | Retained | Payload | When |
|---|---|---|---|
| `<base>/availability` | Yes | `online` / `offline` (last will) | On connect / disconnect |
| `<base>/alerts` | No | One message per alert (with **Settings → Alerts → MQTT** on): `{"event","events"?,"title","message","level","device","timestamp"}` | Each alert |
| `<base>/alerts/armed` | Yes | `1` sending alerts, `0` paused | On change and reconnect |
| `<base>/alerts/armed/set` | - | Subscribed: `1`/`0`, `on`/`off`, `true`/`false` resume or pause alerts | Command |

Booleans are `1`/`0`; availability uses Home Assistant's `online`/`offline`.

<!-- diagram: DIA-09
sources: src/WebServerAlerts.cpp#WebServer::publishArmedState src/WebServerStatus.cpp#WebServer::publishDiscovery src/MQTTClient.cpp src/AlertDispatcher.cpp#AlertDispatcher::dispatch
blocking: false
fingerprint: 2b2421daf3cbfadc
-->
<figure class="diagram" markdown>

```mermaid
flowchart LR
    accTitle: MQTT topic map
    accDescr: RoofRoof listens on alerts/armed/set and publishes availability, alerts and alerts/armed under its base topic, plus Home Assistant discovery under the discovery prefix.
    SET["alerts/armed/set<br/>1/0, on/off, true/false"] -->|command| D["RoofRoof"]
    D --> AVAIL["availability: online or offline<br/>retained, last will"]
    D --> ALERTS["alerts<br/>not retained, one message per alert"]
    D --> ARMED["alerts/armed: 1 sending, 0 paused<br/>retained, on change and reconnect"]
    D --> DISC["homeassistant/.../config<br/>retained, when discovery is on;<br/>the Connectivity sensor always, the Alerts switch only while Send alerts is on"]
```

<figcaption>MQTT topic map: the one topic RoofRoof listens to (left) and what it publishes (right). All but the discovery topics sit under the base topic, default <code>roofroof</code>.</figcaption>
</figure>

??? info "Diagram in words"

    Under the base topic (default `roofroof`), RoofRoof publishes:

    - `availability` - `online`, or `offline` as the last will, retained;
    - `alerts` - one message per alert, not retained (with the MQTT alert channel on);
    - `alerts/armed` - `1` sending / `0` paused, retained, on change and on reconnect.

    It listens on `alerts/armed/set` (`1`/`0`, `on`/`off`, `true`/`false`) to resume or pause alerts.

    With Home Assistant discovery on, it also publishes retained `config` topics for the Connectivity sensor and the Alerts switch under the discovery prefix (default `homeassistant`), and clears them when discovery is switched off or moved. The sensor is always announced; the switch is only announced while **Send alerts** is on, and is removed otherwise.

!!! note "The same in every language"
    Topics and payloads never change with the UI language, including Home Assistant discovery. Alert titles and messages are English (or your own alert wording) for people to read: automate on the alert `event`.

---

## Home Assistant

Turn on **Settings → Network → MQTT → Home Assistant → MQTT discovery**. RoofRoof then appears as a device (named after the device name) with:

- **Connectivity** (binary sensor): connected while the device is `online`, disconnected when it goes `offline`. It is announced whenever discovery is on, so the device shows up even with **Send alerts** off.
- **Switch**: *Alerts* - on while sending, off while paused (see [When to send](alerts.md#when-to-send)). It is announced only while **Send alerts** is on.

The Alerts switch is unavailable while the device is offline. The discovery prefix defaults to `homeassistant`. The roof's own entities *come in a later spec*.

See [Alerts](alerts.md) for the alert events.

---

## Mosquitto Test

```bash
mosquitto_sub -h <broker-ip> -t 'roofroof/#' -v
```
