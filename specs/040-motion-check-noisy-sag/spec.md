# Fix: The Movement Check Under a Noisy Sag (F34)

**Branch**: `fix/040-motion-check-noisy-sag` | **Amends**: spec 036 FR-014 | **Constitution**: 9.1.0, I (unchanged)

## Field finding

F34, on the real roof: a normal full-speed open (move record: target open, 21.5 s, Hall 1133 to
1791, whole-move sag 1.23 V) latched `noMovement` 21 s in with the roof part-open: "No movement
detected: supply sag 0.63 V < 0.20 V and the Hall reading unchanged". The 1 Hz telemetry shows the
supply between 12.09 and 12.24 V against a 12.4 V rest, so the sag swung around the 0.2 V
threshold, with dips as deep as 0.63 V.

Spec 036 counted a sag only when it stayed at `roof.motionSagVolts` for `CONFIRM_MS` (250 ms). With
the motor on PWM the supply reading is noisy, so every reading under the threshold restarted that
hold and it never confirmed. The Hall reading is flat between the magnets by design, so after
`roof.motionCheckSeconds` the check latched. The text also read as false: 0.63 V is not under
0.20 V. It gave the deepest sag, which had passed the threshold, but not for long enough.

## Change

- **FR-014 (amended)**: the window restarts on any supply reading, after the first
  `BLIND_SURGE_MS` of driving, that sags by `roof.motionSagVolts` or more. That is the window's
  peak sag, not a sag held `CONFIRM_MS`. A Hall change of `MOTION_HALL_COUNTS` still restarts it
  too, and the start-up surge still doesn't count.
- F21 still faults: a slow duty too low to move the roof barely sags the supply and leaves the Hall
  reading flat, so no reading reaches the threshold.
- The fault text names what was missing: "No movement detected: no supply sag of 0.20 V or Hall
  change (deepest sag 0.05 V)". It still gives the deepest sag as measured and the setting as the
  limit, so the record format is unchanged.
- Nothing is allocated on the control path. The check keeps one less field (its pending hold).

## Trade-off

One noisy reading at the threshold now restarts the window. A supply that is noisy at rest by that
much would hide a stuck roof until the hard timeout or the overrun check, which still apply. Keep
`roof.motionSagVolts` above the supply's noise at rest and below the sag at the slow duty.

## Tests (`test/test_roof_motion_sag`)

- A sag alternating 0.1 / 0.6 V each step: no fault in 30 s (it faulted before the fix).
- A sag at 0.15 V with a 0.63 V reading every 3 s: no fault (it faulted before the fix).
- A flat supply: `noMovement` after the surge and the check time, deepest sag 0.
- A 1.5 V sag only in the surge, then 0.05 V: still `noMovement`.
- Spec 036's movement tests are unchanged and pass.
