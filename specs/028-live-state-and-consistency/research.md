# Research: Honest Live State and Consistent Secondary Text

## R1. Why the diagnostics page looked faster

- **Decision**: one schedule, 2 s at rest and 500 ms while moving, for both streams.
- **Finding**: `WebServer::handle` sends `/ws/status` every 2 s (`WS_STATUS_BROADCAST_INTERVAL_MS`)
  and at once, at most every 250 ms, when `RoofControl::revision()` changes. The revision changes
  only on a state, fault, waiting-close or calibration change, not with the position. `/ws/diag`
  follows `Diag::streamIntervalMs`: 250 ms while opening or closing. So during a move the diagnostics
  page got 8 readings for each of the dashboard's. Measured on the bench board at rest: both streams
  send their first message within about 100 ms of the socket opening and then every 2 s.
- **Rationale**: the status document is about 4.1 KB, the diagnostics one about 0.9 KB. Three
  status clients at 4 a second would be about 48 KB/s on the controller's Wi-Fi during every move,
  and a client whose queue stays full is closed after 10 s (`STALL_CLOSE_MS`). At 2 a second it is
  about 24 KB/s, and the position bar moves about 19 mm between messages at 38 mm/s.
- **Alternatives**: 250 ms for both (twice the traffic for little visible gain); keep diag at 250 ms
  and raise only status (still two rates).

## R2. Why the diagnostics page showed old readings

- **Finding**: `LivePage` used `data?.enabled ? data : initial`, where `initial` is the document
  fetched when the page opened. `useWebSocket` keeps the last message after a socket closes, but a
  new page's socket starts empty, so whenever the stream had nothing, the page showed the page-load
  document. On a phone, a tab restored from the browser's page cache, or woken from sleep, keeps its
  socket object until the browser notices it is dead, and then waited 5 s to retry.
- **Decision**: the hook clears its reading when the socket closes, reports `connecting` or
  `reconnecting`, closes a socket quiet for 5 s, and reconnects at once on `visibilitychange`
  (visible), `pageshow` and `online`. Pages use the page-load document only while `connecting`.

## R3. Why the tooltip was covered

- **Finding**: `.masonry-item` is absolutely positioned and moved with `transform`, which makes each
  dashboard card a stacking context. The tip's `z-index: 20` only orders it inside its own card; the
  next card in the DOM is painted over it. `.diag-table-wrap` (overflow) clips tips in it.
- **Decision**: show the tip with the popover API (`popover="manual"`, `showPopover()`), which puts it
  in the top layer above every stacking context, and position it with `position: fixed` coordinates
  from the "?"'s bounding box, above it, or below when there is no room, clamped to the window.
  Without `showPopover` (old browsers, jsdom) the CSS placement stays.
- **Alternatives**: `:has()` raising the hovered card's z-index (fixes only the cards it names, not
  overflow clipping); a portal to `body` (moves the tip out of its button, so `aria-describedby`
  wiring and Escape handling need rework).

## R4. Secondary text

- **Finding**: `.note` 0.76 rem dim, `.metric-sub` 0.72 rem muted, `.system-subtle` 0.76 rem dim,
  `.choice-detail` 0.82 rem muted, `.modal-aside` 0.85 rem muted, page intro 0.86 rem muted,
  `.event-list p` 0.78 rem muted. A `Note` after a `ReadingRow` sits under the row's divider.
- **Decision**: tokens `--subtext-size: 0.76rem`, `--subtext-line: 1.4`, `--subtext-color: var(--dim)`
  in `:root`; one grouped rule sets them on `.subtext` and on each class above (tone classes keep
  their colour); `.system-subtle` is replaced by `.subtext`; a row followed by a note gives its divider
  to the note (`:has(+ .note)`). The banner's detail stays (safety state, DS-14).

## R6. Paying for it in the bundle

- **Finding**: the device bundle (gzipped JS + CSS) was 163,007 bytes on main, LittleFS at its 75%
  limit. The new hook, tip and waiting state added about 550 bytes.
- **Decision**: the hook keeps one state object per subscriber; the Status card's freshness row and
  `useQuiet` go (unreachable now); strings with identical text share one key (the "Could not reach
  the device" variants, five settings-dependency labels, the Diagnostics card's title and button,
  the wizard's no-alert-channel note). Result: slightly under main.

## R5. Night clock for the wizard test

- **Finding**: the demo device's clock starts at `Date.now()`; its site follows the demo SQMeter's GPS
  fix (51.4769 N, 0.0005 W). In daytime the daylight check reports day, and the test's later move
  steps are refused.
- **Decision**: `page.clock.install({ time: '2026-12-21T23:00:00Z' })` before the page loads in that
  test; the clock then runs naturally.
