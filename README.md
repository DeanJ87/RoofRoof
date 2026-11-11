# RoofRoof

ESP32 firmware for a roll-off observatory roof: an ASCOM Alpaca Dome for N.I.N.A. that closes
the roof when the weather turns unsafe, but only onto a parked mount.

Built on the SQMeter platform. The rules every change is checked against are in
[`.specify/memory/constitution.md`](.specify/memory/constitution.md).

**Status:** platform only (spec 001). The device serves its web UI, REST API, live status, MQTT with Home
Assistant discovery, an Alpaca server (no device yet) and push alerts, and it holds the motor driver off from
boot. It does not move a roof yet; the motor, safety rules and Dome device come in later specs.

## Where to look

- [`docs/hardware/wiring.md`](docs/hardware/wiring.md): the pin table and the required 10 kΩ pull-down on the motor driver's EN pin
- [`BUILD.md`](BUILD.md) and [`DEV_WORKFLOW.md`](DEV_WORKFLOW.md): build, flash and develop
- [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`SECURITY.md`](SECURITY.md)
- [`docs/`](docs/index.md): the documentation. The repository is private, so there is no public site: CI builds the docs and the live demo and uploads them as the `site` artifact of the **Docs & Demo** workflow.
