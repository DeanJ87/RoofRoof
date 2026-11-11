# Quickstart: Data Interfaces

## 1. Local checks

Everything in [spec 001's quickstart §1](../001-platform-bootstrap/quickstart.md) passes. The new
native suite is `test_roof_mqtt`; `test_dome_bridge`, `test_dome_close`, `test_roof_logic`,
`test_roof_status`, `test_ha_discovery` and the config suites are extended.

## 2. Contract schemas

```sh
cd web && CONTRACT_SAMPLES=/tmp/rr-samples npx vitest run contracts && cd ..
python3 tools/contracts/generate_schemas.py /tmp/rr-samples
git diff specs/001-platform-bootstrap/contracts/schemas   # only the roof section's new keys
```

## 3. Bench (motor disconnected, broker on the LAN)

```sh
mosquitto_sub -h <broker> -t 'roofroof/#' -v
```

1. MQTT on, Home Assistant discovery on. `roofroof/state` arrives retained with `roof.state`,
   `closePhase`, `weather` and `parkConfirmed`. Home Assistant lists the Roof cover (read-only),
   Roof position, Supply voltage, Hall reading and Roof fault.
2. `mosquitto_pub -t roofroof/roof/set -m OPEN` → `roofroof/roof/result` says `switchedOff`.
3. Turn on **Accept roof commands**. The cover gains its buttons. OPEN with the weather safe →
   accepted, `state` goes `opening`. STOP → accepted.
4. `mosquitto_pub -r -t roofroof/roof/set -m OPEN`, then restart the device → the result says
   `retained` and nothing moves. Clear it: `mosquitto_pub -r -n -t roofroof/roof/set`.
5. Weather unsafe and the mount parked: the automatic close starts. STOP from MQTT → refused with
   `unsafeClose` and the roof keeps closing. STOP in the web UI stops it.
6. Position unknown (power up between the magnets): OPEN and CLOSE → refused (`positionUnknown`).
7. A latched fault: the Roof fault sensor is on with the fault's text in its attributes.
