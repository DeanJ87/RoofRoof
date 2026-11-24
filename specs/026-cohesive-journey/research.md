# Research: spec 026

## R1 Root causes (from journey-audit.md)

- **Held after Stop**: `CloseSeq::Sequence::stoodDown` ends `HeldByPerson` only when the roof is
  closed or it has been safe for `rearmSafeMs` since the stop. The calibration run and the jog refuse
  while the sequence isn't idle (`attendedRefusal`, `closeSequence`). The page showed only the
  checklist row "Automatic close idle".
- **Timeout**: `Roof::moveTimeoutMs` = the profile's expected time for the distance + margin; for
  1850 mm at 38 mm/s with 2 s and 3 s ramps that is 65.9 s (checked against the demo's fault text).
- **SQMeter overwrite**: the device's keep rule (`Site::keepFollowed`) put the synced values back
  over a save to a following group, and the next sync replaced the user name and cleared the
  password; only some fields were read-only.
- **Test box close**: the firmware applies one save atomically (one `SafetyInputs::Settings`); the
  close came from two saves (source off, then bypass on), because the page blocked the bypass beside
  an SQMeter source.

## R2 Release hold

Rejected: relaxing the jog and calibration gate while held (the hold would no longer protect a
person who stopped a close; a jog could start in a window where the sequence would otherwise act).
Rejected: release while unsafe (would restart the close against the person). Chosen: release only
while nothing wants the roof closed, attended, applied by the sequence's next step. The effect equals
the end of the re-arm spell, which the device would reach by itself.

## R3 Budgets

- LittleFS was exactly at its 75% limit. The `device.*` catalogue (English to the same English) was
  ~7 KB of the gzipped script; leaving the messages no code names out of the build frees two 4 KB
  blocks. Measured: 336 KB → 332 KB on LittleFS, JS + CSS 160.7 KB → 157.9 KB gzipped, with this
  spec's UI included. Merging the entry chunk into the app (inlineDynamicImports) was tried and
  grew the script; not used.
- Flash: +1.8 KB at the first firmware change (92.3% → 92.4%), for the release route, the countdown,
  `roof.measured` and the follow rule; inside SIZE-04's warning band, far from the 95% fail.

## R4 Move limits on the page

Ported `expectedMoveMs` and `unknownMoveTimeoutMs` to `web/src/lib/moveLimits.ts` (display only);
the test pins the default roof at the device's 65.9 s.
