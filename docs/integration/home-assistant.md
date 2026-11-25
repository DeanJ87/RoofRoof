---
description: "Every MQTT topic RoofRoof publishes or subscribes to and every Home Assistant entity it announces: ids, types, units, the state field each reads and when it is available, with an example dashboard card."
---

# Home Assistant and MQTT topics

This page lists everything RoofRoof puts on an MQTT broker and everything it announces to Home Assistant. Setting it up, the roof commands and their limits are in [MQTT](../user-guide/mqtt.md).

Under the **base topic** (**Settings → Network → MQTT → Base topic**, default `roofroof`; written `<base>` below) the device uses these topics. Payloads and topic names never change with the UI language.

## Topics

| Topic | Direction | Retained | Payload |
|---|---|---|---|
| `<base>/availability` | published | yes | `online`; `offline` is the last will, set at connect |
| `<base>/state` | published | yes | The state document: `{"timestamp", "timeValid", "roof": {...}}` (below) |
| `<base>/roof/set` | subscribed | never acted on when retained | `OPEN`, `CLOSE` or `STOP`, any case, with **Accept roof commands** on |
| `<base>/roof/result` | published | no | One answer per message on `roof/set`: `{"command", "accepted", "reason", "text", "state"}` |
| `<base>/alerts` | published | no | One message per alert, with **Settings → Alerts → MQTT** on: `{"event", "events"?, "title", "message", "level", "device", "timestamp"}` |
| `<base>/alerts/armed` | published | yes | `1` sending alerts, `0` paused |
| `<base>/alerts/armed/set` | subscribed | no | `1`/`0`, `on`/`off` or `true`/`false` resumes or pauses alerts |
| `<base>/diag` | published | no | The live [diagnostics](../user-guide/diagnostics.md) document, with **Diagnostics over MQTT** on, every 60 s at rest |
| `<base>/diag/move` | published | no | One finished move's summary, with **Diagnostics over MQTT** on |
| `<discovery prefix>/<component>/<device id>/<object>/config` | published | yes | Home Assistant discovery, with discovery on (prefix default `homeassistant`); emptied when discovery is switched off or moved |

With **SQMeter over MQTT** as a weather source, the device also subscribes to SQMeter's own `<sqmeter>/availability`, `<sqmeter>/safe` and `<sqmeter>/safety` (base topic `safety.mqtt.topic`, default `sqmeter`). It never publishes to them.

The `roof` object of the state document is the status's `roof` object ([REST API](../api/rest.md#get-apistatus)) plus `closeRearmSeconds`, `daylightState` (`off`, `unknown`, `night` or `day`) and `blocked` (a line saying why the roof can't move now, or `""`), and without the long `text` fields of `nina` and `sqmeter`. While the roof moves, the document is published as the move starts (within a fraction of a second: `state` `opening` or `closing`, `driving`, `target`, the starting `positionPercent`), every 3 s with the position, and at a reversal, fault or the end; the Roof cover shows opening and closing. While a close waits with the roof still, it is published only when that starts or ends.

## Entities

With **Home Assistant → MQTT discovery** on, the device announces itself as one Home Assistant device named after the device name (identifier `roofroof_<mac>`, manufacturer and model "RoofRoof", the firmware version). Twenty entities are announced.

**Entity ids.** Home Assistant builds an entity id from the device name and the entity name, so with a device named `roofroof` the weather sensor is `binary_sensor.roofroof_weather_safe`; a device named `Observatory roof` gives `binary_sensor.observatory_roof_weather_safe`. Check the id in Home Assistant's entity list before using it. Each entity also carries a fixed `unique_id` and `object_id` of the form `roofroof_<mac>_<object>` (the `<mac>` is the device's MAC address without separators, as in its `UniqueID`), which is the `<object>` column below: `roofroof_<mac>_weather_safe`.

Unless a row says otherwise, an entity is available while `<base>/availability` is `online`; a `null` in the state document shows as unknown. A sensor with a unit reports its state as a measurement.

| Entity (name) | Component, `<object>` | Class and unit | Reads | Available |
|---|---|---|---|---|
| Connectivity | `binary_sensor`, `connectivity` | connectivity | `<base>/availability`: on for `online`, off for `offline` | always (it is the availability) |
| Alerts | `switch`, `alerts` | - | state `<base>/alerts/armed` (`1` on); turns on or off by publishing `1`/`0` to `<base>/alerts/armed/set` | device online; announced whenever discovery is on |
| Roof | `cover`, `roof` | - | `roof.state`: open, opening, closed, closing or stopped; `unknown` and `error` show as unknown. With **Accept roof commands** on, Open, Close and Stop publish `OPEN`, `CLOSE`, `STOP` to `<base>/roof/set`; otherwise read-only. No position slider | device online |
| Roof position | `sensor`, `position` | %, measurement | `roof.positionPercent` (unknown while the position is) | device online |
| Supply voltage | `sensor`, `supply` | voltage, V | `roof.supplyVolts` (unknown when unreadable) | device online |
| Motor current | `sensor`, `motor_current` | current, A | `roof.motorAmps` | device online, and only announced while current sense is on |
| Hall reading | `sensor`, `hall` | raw counts, diagnostic | `roof.hall.counts` | device online |
| Roof fault | `binary_sensor`, `fault` | problem | on while `roof.fault` is set; attributes are the fault (`kind`, `cause`, `measured`, `limit`, `text`) | device online |
| Needs a person | `binary_sensor`, `needs_person` | problem | on while `roof.needsPerson.active`: a restart with the position unknown or a fault latched, the Hall sensor failing, or the automatic close unable to move; the reason is in the Roof needs a person alert and the web UI's banner | device online |
| Test box bypass | `binary_sensor`, `test_box` | problem | on while `roof.testBox.active`; attributes are `roof.testBox` | device online |
| Weather safe | `binary_sensor`, `weather_safe` | - | on while `roof.weather` is `safe` | device online |
| Mount parked | `binary_sensor`, `mount_parked` | - | on while `roof.parkConfirmed` | device online |
| Automatic close | `sensor`, `close_phase` | text | `roof.closePhase`: `idle`, `waitPark`, `closing`, `retryWait`, `heldByPerson`, `cannotMove` or `halted` | device online |
| Automatic close held | `binary_sensor`, `close_held` | problem | on while `roof.closePhase` is `heldByPerson` or `halted` | device online |
| Hold lapses in | `sensor`, `hold_lapses` | duration, s | `roof.closeRearmSeconds`: seconds until a held close lapses; unknown when nothing is counting down | device online |
| Roof blocked | `sensor`, `blocked` | text | `roof.blocked`: why the roof can't move now; empty when nothing blocks it | device online |
| Waiting for park | `sensor`, `park_wait` | duration, s | `roof.parkWait.leftSeconds` while an Open or Close waits for the park, else 0 | device online |
| Daylight | `sensor`, `daylight` | text | `roof.daylightState` | device online |
| SQMeter last reply | `sensor`, `sqmeter_reply` | duration, s, diagnostic | `roof.sqmeter.lastReplySeconds`; unknown without a good reply | device online |
| Checks overridden | `binary_sensor`, `override` | problem | on while a person's override is in effect (`roof.override` not empty) | device online |
| N.I.N.A. reachable | `binary_sensor`, `nina_reachable` | connectivity, diagnostic | on while `roof.nina.reachable` | device online |

Discovery is published again on every reconnect and when a setting it depends on changes, but not while the roof moves. Everything else on the page is also on the REST API, so an entity that Home Assistant lacks can be a [REST sensor](../api/rest.md#get-apistatus) instead.

!!! note "Home Assistant can't override a check"
    The cover's Open and Close go through MQTT, with the same checks as every command: the weather, the park (they wait for it when N.I.N.A. can be asked), daylight and the motion checks. There is no override, no way to clear a fault and no Release hold from Home Assistant: those are a person's at the web UI.

## Example dashboard card

An entities card and a tile for the roof, with a device named `roofroof` (change the ids to match yours):

```yaml
type: vertical-stack
cards:
  - type: tile
    entity: cover.roofroof_roof
    features:
      - type: cover-open-close
  - type: entities
    title: Roof
    entities:
      - entity: binary_sensor.roofroof_weather_safe
        name: Weather safe
      - entity: binary_sensor.roofroof_mount_parked
        name: Mount parked
      - entity: sensor.roofroof_roof_blocked
        name: Why it can't move
      - entity: sensor.roofroof_automatic_close
        name: Automatic close
      - entity: sensor.roofroof_hold_lapses_in
        name: Hold lapses in
      - entity: sensor.roofroof_waiting_for_park
        name: Waiting for park
      - entity: sensor.roofroof_daylight
        name: Daylight
      - entity: binary_sensor.roofroof_roof_fault
        name: Fault
      - entity: binary_sensor.roofroof_needs_a_person
        name: Needs a person
      - entity: sensor.roofroof_sqmeter_last_reply
        name: SQMeter last reply
      - entity: binary_sensor.roofroof_n_i_n_a_reachable
        name: N.I.N.A. reachable
      - entity: switch.roofroof_alerts
        name: Alerts
```

An automation can use the alert topic instead of an entity: trigger on a message on `roofroof/alerts` and match the `event` field (for example `nina_lost`, `mount_not_parked`, `roof_not_closed`, `needs_person`). Alert titles and messages are for people to read; automate on `event`.
