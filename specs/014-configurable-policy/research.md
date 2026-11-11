# Research: Configurable Policies

## R1. One policy struct, clamped by the logic

- **Decision**: `CloseSeq::Policy` holds every tunable value of the automatic close in
  milliseconds, with defaults equal to the constants of specs 005 and 013 (which stay as the
  named defaults). `Sequence::setPolicy` clamps each value to its range before storing it.
- **Rationale**: the config validates on save, but the logic must not depend on that (a value
  from an older or newer release, the demo, a test). Clamping in the logic makes "no value in
  the range can break (a)" a property of the logic itself.
- **Alternatives**: passing the config into the sequence (couples the pure logic to the config
  layout); separate setters (more surface, easy to miss one).

## R2. One re-arm spell for every 10-minute use

- **Decision**: `REARM_SAFE_MS` becomes `policy.rearmSafeMs` everywhere (stand-downs, the pause
  window, both wall-clock caps, spec 013's hand-over and its `weatherPauseValid`).
- **Rationale**: these uses were designed together (spec 013's review rounds 3 to 8 depend on
  "when spec 005 would have ended the episode"). One value keeps that reasoning valid at every
  setting.
- **Alternatives**: a stand-down-only setting. With daylight "alert and close", a short
  stand-down would be held up to 10 minutes by the hand-over while spec 005 alone would have
  re-armed it: an untested mix.

## R3. Ranges

| Value | Range | Floor and ceiling reasons |
|---|---|---|
| Re-arm spell | 5–60 min | Below 5, short safe spells restart attempts with a fresh park limit; above 60 a stop holds back too long |
| Park request spacing | 10–120 s | N.I.N.A. needs time to act on a request; the park limit (30 s minimum) still runs |
| Park hold | 4–30 s | Never below spec 005's 4 s: with N.I.N.A. polled every 2 s a shorter hold could rest on one reading (II); 30 s is an eighth of the default park limit |
| Back-off | 1–30 min | A retry still needs the park confirmed; the wall-clock cap is back-off plus re-arm |
| Close start | 25–120 s | A Wi-Fi change holds a close up to 15 s (spec 010), which keeps a 5 s margin under it |
| Close guard | 10–120 s | Added to the move's own timeout, which the motion logic enforces first (I) |
| Reminder | 10–120 min | Never off; at least the alert pipeline's own retry span |
| MQTT change gap | 1–60 s | At rest only |
| Daylight band | 0.1–2.0° | Never 0 (flicker); 2° is about 10 minutes of the sun's motion |

## R4. Not-started retry

- **Decision**: a new `Cause::NotStarted` (index 10, after the existing ones so stored records
  keep their meaning), event `CloseFailed`, action `Stop` (cancels the waiting close, as the halt
  does), then `RetryWait` with the close back-off, like a refused close.
- **Rationale**: the stop is needed, or the waiting close would start later by itself while the
  sequence backs off, and the sequence would see it as a new close.
- **Restart**: a back-off with this cause is saved as any back-off; a downgrade reads the unknown
  cause as an invalid record and starts afresh (an attempt, the closing direction).

## R5. Remote stop

- **Decision**: `CommandContext::remoteStopsClose` (default false). When true, a remote stop that
  the client rule would refuse is accepted with `standsDown = true`; `RoofControl` and the demo
  count it in `personStops`, so the sequence stands down with "Automatic close stopped" and its
  reminders. Every other remote stop is unchanged and never counts. The Alpaca simulator keeps the
  default context.
- **Alternatives**: a separate "client stop" event (new alert, new wording, more surface); kept
  fixed (the owner asked for it as an option; the person's-stop path already guarantees the
  alert).

## R6. Alert wording

- **Decision**: `Decision` carries `retryMs` and `rearmMs` (filled by `step` from the policy), so
  texts say "tries again in N minutes" and "safe for N minutes" with the set values; with the
  defaults the words are today's.

## R7. Alert resend cap

- **Decision**: `AlertRetry::setMaxGap(ms)` from the reminder interval; first gap and pending
  limit fixed.

## R8. Differential fuzz

- **Decision**: `tools/close-diff/run.sh` exports `origin/main`'s `lib/` into a temporary
  directory, compiles one driver against each tree, runs both on the same seeds and compares
  their traces. The driver uses only API both trees have (no `setPolicy`), so the branch runs its
  defaults.
- **Rationale**: two copies of the same symbols can't share one binary; two binaries and a text
  diff can.

## R9. Advanced fold

- **Decision**: a `Fold` control in `settings/controls.tsx` on the native `<details>` and
  `<summary>` (keyboard and screen-reader support built in), styled with existing tokens. Its
  summary reads "Advanced". It opens by itself when a field inside has an error.

## R10. Alpaca default

See the spec's "Alpaca's default": off, with a note and a doc anchor.
