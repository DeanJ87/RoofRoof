# RoofRoof

ESP32 firmware for a roll-off observatory roof: an ASCOM Alpaca Dome for N.I.N.A. that closes
the roof when the weather turns unsafe, but only onto a parked mount.

Built on the SQMeter platform. The rules every change is checked against are in
[`.specify/memory/constitution.md`](.specify/memory/constitution.md).

**Status:** in development. The device serves its web UI, REST API, live status, MQTT with Home Assistant
discovery, the roof as an ASCOM Alpaca Dome and push alerts, and it holds the motor driver off from boot. The
roof opens and closes on a person's or an imaging app's command, within the safety rules, and closes by itself
onto a parked mount when the weather turns unsafe.

## Where to look

- [`docs/hardware/wiring.md`](docs/hardware/wiring.md): the pin table and the required 10 kΩ pull-down on the motor driver's EN pin
- [`BUILD.md`](BUILD.md) and [`DEV_WORKFLOW.md`](DEV_WORKFLOW.md): build, flash and develop
- [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`SECURITY.md`](SECURITY.md)
- [`docs/`](docs/index.md): the documentation. There is no public docs site: CI builds the docs and the live demo and uploads them as the `site` artifact of the **Docs & Demo** workflow.
