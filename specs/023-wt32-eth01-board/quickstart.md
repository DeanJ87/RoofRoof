# Quickstart: Board Presets, Pin Setup and WT32-ETH01 Ethernet

## Native tests

```bash
~/.platformio/penv/bin/pio test -e native -f test_board_pins -f test_wiring_check -f test_mdio -f test_network_plan -f test_config_model -f test_safety_input_logic
```

## Web

```bash
cd web && npx vitest run Pins pinsApi Wizard NetworkTab && npx playwright test
```

## Bench (pending, real boards)

- DevKit, upgraded from the previous release: the boot log names the DevKit pins; the roof behaves
  as before.
- DevKit: run the wiring check with the 10 k EN pull-down fitted (EN passes) and removed (EN fails);
  a logic analyser on GPIO25/26/27 shows RPWM and LPWM low throughout and EN never driven high.
- DevKit: save a Custom set with EN on GPIO2 → refused; edit the stored settings to put EN on
  GPIO6 (backup restore of a hand-edited file) → after the restart, no pin driven, "Pin setup
  invalid", Settings reachable; fix and restart → normal.
- WT32-ETH01 freshly flashed (DevKit preset): the wiring check's Ethernet probe finds the LAN8720
  and suggests the carrier preset.
- WT32-ETH01 with the carrier preset and a cable: an address over Ethernet within 5 s; the web UI,
  `roofroof.local`, MQTT and Home Assistant, Alpaca discovery from N.I.N.A., SQMeter and N.I.N.A.
  polling, an ntfy test, NTP and command-line OTA all work with Wi-Fi off.
- WT32-ETH01, Wi-Fi fallback: unplug the cable for 30 s → the station joins; plug it back →
  N.I.N.A. polls go over Ethernet again (router's view or the status `ethernet.link`).
- WT32-ETH01, Wi-Fi off, no cable, restart → the setup hotspot after 45 s.
- WT32 carrier: the wiring check passes EN (carrier pull-down), Hall, supply, park inputs and
  current sense at rest.
