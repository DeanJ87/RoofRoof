# Data model: spec 026

## Device

| Field | Where | Meaning |
|---|---|---|
| `roof.measured` (uint8, settings) | `RoofConfig::measured`, JSON `roof.measured` | Bits 1 travel, 2 speed, 4 timeout margin, 8 Hall end and thresholds, 16 opening direction: measured on the roof and not typed since. Default 0; unknown bits dropped. |
| `roof.closeRearmSeconds` (status) | `RoofIo::Snapshot::closeRearmSeconds`, `writeCloseRearm` | See contracts/release-hold.md. |
| `CloseSeq::Inputs::releaseHold` | lib | One step's attended release request. |
| `CloseSeq::Sequence::standDownLeftMs()` | lib | The countdown, `NOT_COUNTING` when not counting. |

### Rules (lib/ConfigModel, on a save from the UI or API)

- `roof.measured`: when the save doesn't send it, the bit of each measured value the save changes is
  cleared (`typedOver`). Teach confirm sets bit 8 on the device; the page sends the bits with an
  accepted calibration run (plus 16) and with the wizard's "it opened" answer (16).
- SQMeter groups (`time`, `mqtt`, `alerts`): when the save changes a following group's values and
  doesn't send `sqmeterSync.<group>.follow`, the group is made local (`unfollowEdited`). The values
  compared: time zone and servers; broker, port, user, password, MQTT on, SQMeter's topic; ntfy
  server, topic, token, on; Pushover keys, on.

## Page

| Module | What |
|---|---|
| `lib/moveBlockers.ts` | `moveBlockers(roof, 'open' \| 'close' \| 'move', ctx)`, `closePhaseBlocker`, `faultBlocker`, `releaseOffered`, `bypassedChecks` |
| `components/MoveBlockers.tsx` | The list; Release hold's attended confirmation; Clear fault |
| `lib/setupProgress.ts` | `setupStages(config)`, `setupComplete`, `valueSource`, `motionDefaults` |
| `components/SetupProgress.tsx` | The stage rows |
| `lib/moveLimits.ts` | `expectedMoveMs`, `moveLimits` (the device's MotionProfile, for display) |
| `lib/syncView.ts` `editChanges` | An edit plus the group's unfollow |
| `lib/configPatch.ts` `SYNC_GROUP_FIELDS`, `withSentTogether` | The follow flag sent with a group's changes |
| `vite.i18n.ts` | Leaves unnamed `device.*` messages out of the build |
