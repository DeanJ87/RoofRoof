---
title: Roll-off roof controller for ESP32
description: "ESP32 firmware and web UI for an observatory roll-off-roof controller: ASCOM Alpaca for N.I.N.A., MQTT, Home Assistant and alerts. Open, close and stop from the dashboard, N.I.N.A., MQTT or Home Assistant, and an automatic close onto a parked mount."
---

# RoofRoof

<div class="sqm-hero" markdown>
<div class="sqm-hero-value">RoofRoof</div>
<div class="sqm-hero-unit">ESP32 roll-off roof controller</div>

Firmware and a web UI for an observatory roll-off roof: a safe, networked controller you reach from a browser, N.I.N.A. and Home Assistant.
</div>

RoofRoof is firmware for an ESP32 that drives an observatory roll-off roof through an IBT-2 (BTS7960) motor driver. It was started as a copy of the author's SQMeter project, keeping its platform (WiFi setup, the web UI, the REST API, MQTT, alerts, updates, ASCOM Alpaca) and removing everything about sensors and sky quality.

!!! note "Setting up a new roof"
    The device serves its web UI, a REST API, live status, MQTT with Home Assistant discovery, an ASCOM Alpaca server and push alerts, and it holds the motor driver off from the first instruction of `setup()`. **It moves the roof on an authenticated REST command** ([REST API](api/rest.md#roof)) **an ASCOM Alpaca Dome command** ([ASCOM Alpaca](user-guide/alpaca.md)) **or, when switched on, an MQTT command** ([MQTT](user-guide/mqtt.md#roof-commands)): open only while the weather is safe, close only onto a parked mount. The roof's state goes out over MQTT, with a Home Assistant cover. The dashboard's [Roof card](user-guide/dashboard.md#roof) shows it live, with Open, Close and Stop. A new roof is brought into service with the [commissioning guide](hardware/commissioning.md).

There is no public docs site: these docs and the [live demo](live-demo.md) are built by CI and downloaded as an artifact.

<!-- diagram: DIA-01
sources: src/main.cpp#setup include/WebServer.h include/MQTTClient.h include/AlertDispatcher.h include/MotorDriver.h include/TimeManager.h
blocking: false
fingerprint: e7dd3a50e3aaed71
-->
<figure class="diagram" markdown>

```mermaid
flowchart LR
    accTitle: What RoofRoof connects to
    accDescr: The ESP32 holds the motor driver's pins low from boot and drives it on a person's, an imaging app's or (when switched on) an MQTT command, or to close the roof when the weather turns unsafe (or, with the daylight close set, after sunrise), reads the roof's Hall, supply, current, park and rain inputs, keeps its settings and web UI on the device, serves a dashboard and APIs to browsers, publishes the roof's state to an MQTT broker and takes its commands when switched on, serves the roof as an ASCOM Alpaca Dome to clients such as N.I.N.A., and sends alerts to push services. It reads the weather verdict from SQMeter and the mount's park state from N.I.N.A.'s Advanced API. It gets time from the internet by NTP.
    DRIVER["<b>IBT-2 motor driver</b><br/>RPWM, LPWM, EN<br/>held off at boot;<br/>EN has a 10 kΩ pull-down"]
    SENSE["<b>Roof sensors</b><br/>Hall, 12 V supply, current,<br/>park switches, rain contact"]
    ESP["<b>ESP32 running RoofRoof</b><br/>settings in NVS,<br/>web UI in LittleFS,<br/>closes on unsafe weather<br/>and, if set, in daylight"]
    WEB["Browser<br/>dashboard, REST, WebSocket"]
    MQTT["MQTT broker<br/>Home Assistant discovery"]
    ALPACA["ASCOM Alpaca client<br/>e.g. N.I.N.A."]
    PUSH["Push alerts<br/>ntfy, Pushover, webhook"]
    NET["Internet<br/>NTP time"]
    SQM["SQMeter<br/>weather verdict, HTTP or MQTT"]
    NAPI["N.I.N.A. Advanced API<br/>mount parked?"]
    ESP -->|"GPIO25, 26, 27"| DRIVER
    SENSE -->|"GPIO4, 32-36, 39"| ESP
    NET -.->|time| ESP
    ESP <--> WEB
    ESP <-->|roof state and commands, availability, alerts| MQTT
    ESP <-->|Dome: shutter state;<br/>open, close, stop| ALPACA
    ESP --> PUSH
    SQM -->|safe or unsafe| ESP
    ESP <-->|mount info, park| NAPI
```

<figcaption>What RoofRoof connects to: the motor driver it holds off, the ESP32 in the middle, and everything it talks to.</figcaption>
</figure>

??? info "Diagram in words"

    - **The ESP32** keeps its settings in NVS and the web UI's files in LittleFS.
    - **The motor driver** (IBT-2): the firmware sets GPIO27 (EN), GPIO25 (RPWM) and GPIO26 (LPWM) low as the first thing at boot. EN also has a hardware 10 kΩ pull-down, so the driver stays off before the firmware runs. The roof's control task drives it on a person's command (web UI or REST), on an imaging app's command over Alpaca, on an MQTT command when those are switched on, or to close the roof onto a parked mount when the weather turns unsafe or, with the daylight close set, after sunrise ([Close on unsafe weather](user-guide/close-on-unsafe.md)), every 20 ms, with a watchdog that drops EN if the task stalls.
    - **Roof sensors:** the Hall sensor, the 12 V supply and (when fitted) the motor current on the ADC, the park switches and the rain contact on GPIO inputs ([Wiring](hardware/wiring.md)).
    - **Outputs:**
        - Browsers get the dashboard, the REST API and live WebSocket status.
        - An MQTT broker gets the roof's state, availability and alerts, with Home Assistant discovery of a roof cover and its sensors; it can also switch alerts on and off and, when switched on, open, close and stop the roof within the same safety rules.
        - ASCOM Alpaca clients such as N.I.N.A. find the roof as a Dome, and can open, close and stop it within the safety rules.
        - Alerts go to ntfy, Pushover, a webhook or MQTT.
    - **Inputs:**
        - SQMeter's weather verdict, over HTTP or through the MQTT broker.
        - N.I.N.A.'s Advanced API: whether the mount is parked, and a park request when the roof needs to close.
    - **From the internet:** NTP time only. The device never downloads updates.

![The RoofRoof dashboard](assets/screenshots/dashboard.png)

---

## Features

<div class="grid cards" markdown>

- :material-wifi: **Web Interface**

    Real-time dashboard over WebSocket. No polling. No app required.

- :material-broadcast: **MQTT**

    Availability, alerts and an alerts pause switch for Home Assistant or any broker.

- :material-update: **Updates**

    Upload new firmware or web UI files from the browser, or flash over USB. Nothing is downloaded by the device.

- :material-connection: **ASCOM Alpaca**

    The roof as an Alpaca Dome for N.I.N.A.: open, close and stop, through the same safety checks as the web UI.

- :material-bell-ring: **Alerts**

    Pushover, ntfy, webhook or MQTT, with per-event levels and your own wording, and a pause switch.

- :material-shield-lock: **Safe from boot**

    The motor driver is held off from the first instruction of `setup()`, and EN has a hardware pull-down.

</div>

---

## FAQ

### Does it move a roof yet?

Yes, on a person's command from the web UI or REST API, on an imaging app's command over ASCOM Alpaca, on an MQTT command (Home Assistant) when switched on, and by itself only to close when the weather turns unsafe, or, if you choose it, when the sun is up ([Roof motion](user-guide/roof-motion.md), [Close on unsafe weather](user-guide/close-on-unsafe.md), [Daylight failsafe](user-guide/close-on-unsafe.md#daylight-failsafe)). Every move has a time limit and stops on its magnet.

### Is this SQMeter?

No. RoofRoof started as a copy of the author's SQMeter project (a sky quality meter) and removed its sensors, safety monitor, sky and weather logic, Bluetooth, display, battery, language packs and GitHub release check. It is a separate project with its own repository, partition layout name (`l2`), hostname (`roofroof`) and setup hotspot (`RoofRoof-Setup`). A board that ran SQMeter starts as a new device: RoofRoof's settings live under another NVS namespace and its backups use another key, so SQMeter's settings are ignored (not erased) and an SQMeter backup is refused ([Flashing](getting-started/flashing.md)).

### Does it work with N.I.N.A.?

Yes. N.I.N.A. finds the roof as an ASCOM Alpaca Dome and can open, close and stop it; the roof refuses what the safety rules don't allow and says why ([ASCOM Alpaca](user-guide/alpaca.md)).

### Does it need Home Assistant or MQTT?

No. The device works on its own from the browser. MQTT, with Home Assistant discovery, is optional ([MQTT Integration](user-guide/mqtt.md)).

---

## Quick Start

New device? Go to [Flashing Your Device](getting-started/flashing.md) — you only need a USB cable and the release files. Wire the motor driver's EN pull-down first ([Wiring](hardware/wiring.md)).

Already flashed? Go to [First Boot](getting-started/first-setup.md) to configure WiFi.
