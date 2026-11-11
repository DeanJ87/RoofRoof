# Changelog

All notable changes to RoofRoof are documented here.

## [Unreleased]

### Added

- Platform imported from SQMeter 877208b and stripped to what a roof controller needs (spec 001).
- Safety inputs: the weather verdict from SQMeter (HTTP and/or MQTT) and the rain contact, and the park confirmation from N.I.N.A.'s Advanced API and the park switches; logged on change, set through the settings API (spec 004).
- Close on unsafe weather: when the weather isn't safe and the roof isn't closed, the controller asks N.I.N.A. to park, waits for the confirmed park and closes; every step has a time limit, a person's stop is respected, and the critical failures go out as `roof` alerts; new setting `park.timeoutSeconds` (spec 005).

## [0.0.0] - 2026-11-10

Repository started. No firmware released.
