# Research: Setup Wizard

## R1. The setup state and the upgrade case

**Decision**: `setup.state` (`new`, `offered`, `done`) and `setup.step` (0-9) in a new `SetupConfig`
in `lib/ConfigModel`. `Config::createDefault()` is `new`/0. `Config::markStoredBeforeSetup()`
(pure, tested) is called by `Config::load` (src/ConfigStore.cpp) when the stored document has no
`setup` object, and sets `offered`/0. An unknown state name is refused, never read as a default.

**Rationale**: the only reliable sign of an upgraded device is a stored document without the
section; a device without stored settings is new (first boot or factory reset). A save from the
web UI that leaves `setup` out keeps it (the API applies a partial document on the current
settings), so Settings never touches the state. The state moves nothing and needs no restart.

**Alternatives**: a boolean `setup.completed` (can't tell a new device from an upgraded one, so
either an upgraded roof is interrupted or a new one isn't guided); a browser-side flag (lost with
the browser, and a second browser would see the wizard again).

## R2. When the wizard opens

**Decision**: a `SetupPrompt` in the layout, on the dashboard route only: with `new` and not in
hotspot mode it routes to `/wizard` the first time per page load (a module-level flag); otherwise
it shows the banner for `new` and `offered`. `/wizard`, not `/setup`: the firmware already serves
`/setup` (Alpaca setup pages redirect to Settings) and ESPAsyncWebServer matches by prefix.

**Rationale**: the dashboard is where an owner lands; once per load keeps Stop and the dashboard
reachable. The banner uses the bench banner's slot, outside the card grid (DS-01).

## R3. Saving a page

**Decision**: the wizard shares Settings' editor (a `useConfigEditor` hook taken out of
`Settings.tsx`): the same normalised payload, the same zod validation, the same
`POST /api/config`, with `setup: {state, step}` added. The device's `handleSetConfig` runs
`requireAuth`, `Config::fromJson` (validation) and `saveAsFlashWrite` (spec 010's
`PlatformGuard::FlashWrite`, refused while the roof moves or a close waits). No firmware change
on the save path.

**Rationale**: one save path is one set of rules; a wizard-only endpoint would need its own
motion hold and validation.

## R4. Test without a probe

**Decision**: `GET /api/roof/inputs` returns what the main loop already holds: the weather
verdict with both SQMeter sources' assessed state, the park result, and the poller's last
N.I.N.A. reading. `SafetyInputs::evaluate` (once a second) copies them into a report under its own
mutex; the handler copies the report and writes it with `Safety::writeInputsReport` (pure,
`lib/SafetyInputLogic`, native-tested). Each part carries a `generation`, bumped when its settings
change (the poller's existing counters for SQMeter HTTP and N.I.N.A.; a new counter for SQMeter
MQTT in `SafetyInputs`), because a settings change resets that part's record. Test reads the
document before the save, saves, then reads once a second: for a part whose settings the save
changed, a result is shown only once its generation has moved on and it is no longer waiting;
for an unchanged part, the current reading with its age. 15 s at most.

**Rationale**: the poller already polls SQMeter every 5 s and N.I.N.A. every 2 s, makes a changed
part due at once and drops replies to old settings (generations). Reading the result adds no
outgoing request, so no rate limit, no new time limit and nothing for the async task to wait on
(constitution VI). The generation makes "a reading after the save" exact without clocks.

**Alternatives**: a probe endpoint that fetches a typed URL on demand (an outgoing request per
press, which needs a rate limit, a time limit and a task to run on, and could probe arbitrary
hosts on the LAN); adding the readings to `/api/status` (pushed every second over the
WebSocket to every client for a page few people open).

## R5. Open or protected

**Decision**: open, like `/api/status`: GET only, no body, readings and the device's own texts,
no address, credential or secret, no state change.

**Rationale**: constitution VIII asks for `requireAuth` on endpoints that move, change, send or
reveal more than readings. This reveals readings already summarised in `/api/status`
(`roof.weatherText`, `roof.parkText`). GET can't be triggered with side effects cross-site.

## R6. The supply check

**Decision**: show `roof.supplyVolts`, the 100k/22k divider (a fact) and `roof.minSupplyVolts`
(editable); a meter reading typed by the owner, not saved, compared within 0.2 V.

**Rationale**: the commissioning guide's stage 2 tolerance. A calibration factor would be a new
motion-relevant setting that could hide a low supply.

## R7. Reuse of the Settings cards

**Decision**: the pages reuse the Settings cards where they fit exactly (time zone, NTP, Hall
sensor, current and supply, alert channels) and small exported field groups from the tabs
(password, location, park switches, the weather sources without the stale time, the daylight mode),
so hints, ranges and validation stay in one place. New UI: the page shell, the readings card, the
Test panel and the review.

**Rationale**: constitution VII and the UI size budget (SIZE-01).

## R8. Review checks

**Decision**: a pure `reviewChecks(config, status, inputs, hallNotFitted)` (Vitest) returns one
check per essential with `ok`, `warn`, `bad` or `unknown`, a reason key and its page. Missing
readings give `unknown`, never `ok`.

## R9. Docs links from the device UI

**Decision**: links to the repository's docs (`DOCS_URL` moved to `web/src/lib/docsLinks.ts`;
the demo's `links.ts` re-exports it), opened in a new tab.
