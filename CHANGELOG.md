# Changelog

All notable changes to RoofRoof are documented here.

## [Unreleased]

### Added

- Platform imported from SQMeter 877208b and stripped to what a roof controller needs (spec 001).
- Safety inputs: the weather verdict from SQMeter (HTTP and/or MQTT) and the rain contact, and the park confirmation from N.I.N.A.'s Advanced API and the park switches; logged on change, set through the settings API (spec 004).
- Close on unsafe weather: when the weather isn't safe and the roof isn't closed, the controller asks N.I.N.A. to park, waits for the confirmed park and closes; every step has a time limit, a person's stop is respected, and the critical failures go out as `roof` alerts; new setting `park.timeoutSeconds` (spec 005).
- Roof alerts: seven roof events in Settings → Alerts with their own level, sound and wording: the critical "mount didn't park" and "roof didn't close" (never below urgent, never paused or switched off, sent again until a channel takes them), and a roof fault, a low 12 V supply, a lost weather source, the weather closing the roof and the roof closed afterwards. The `roof` alert event is replaced by `mount_not_parked` and `roof_not_closed` (spec 007).
- ASCOM Alpaca Dome: the roof as a Dome with a shutter (open, close, abort, shutter status, slewing) for N.I.N.A. and other clients, through the same safety checks as the web UI; a client can't move a roof whose position is unknown or clear a fault, and its stop stands the automatic close down; ConformU runs in CI against the simulator's Dome (spec 006). If Alpaca was already on, imaging apps can open, close and stop the roof as soon as this release is installed (within the safety rules); turn Alpaca off under Settings → Network if you don't want that.
- The platform while the roof moves: every flash write (settings, restores, saved values, the roof's fault record) counts as busy, so an open waits and a close waits for it; checks and moves are decided under one lock, so they can't overlap; restarts, firmware uploads and WiFi changes wait for the automatic close; the setup hotspot can't open the roof; MQTT settings are applied on the main loop; the start-up log names the reset reason (spec 010).

## [0.0.0] - 2026-11-10

Repository started. No firmware released.
