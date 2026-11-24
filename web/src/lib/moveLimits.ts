import type { RoofConfig } from '../types';

// Spec 026: a move's hard time limit as the device works it out
// (lib/RoofLogic/src/MotionProfile.cpp: moveTimeoutMs and unknownMoveTimeoutMs),
// so Settings can show it beside travel and speed as they are typed. Display
// only: the motion code enforces its own (constitution I).

type Motion = Pick<
  RoofConfig,
  'travelMm' | 'speedMmPerSecond' | 'timeoutMarginSeconds' | 'rampUpMs' | 'rampDownMs' | 'topDutyPercent' | 'slowDutyPercent'
>;

const STEP_MS = 10; // the device integrates the ramp-up in 10 ms steps
const duty = (percent: number) => Math.min(percent, 100) / 100;

// The duty fraction while ramping up and slowing down near the end (MotionProfile.cpp profileFraction).
const fraction = (m: Motion, drivenMs: number, remainingMm: number, zoneMm: number) => {
  const top = duty(m.topDutyPercent);
  const slow = duty(m.slowDutyPercent);
  if (!(remainingMm > 0)) return slow;
  const up = drivenMs < m.rampUpMs ? slow + ((top - slow) * drivenMs) / m.rampUpMs : top;
  const down = zoneMm > 0 && remainingMm < zoneMm ? slow + ((top - slow) * remainingMm) / zoneMm : top;
  return Math.max(slow, Math.min(up, down));
};

/** The expected time of a full move, in ms (0 for settings the device wouldn't accept). */
export const expectedMoveMs = (m: Motion): number => {
  const speed = m.speedMmPerSecond / 1000; // mm per ms at full duty
  const top = duty(m.topDutyPercent);
  const slow = duty(m.slowDutyPercent);
  if (!(m.travelMm > 0) || !(speed > 0) || !(slow > 0) || top < slow) return 0;
  const zone = speed * top * m.rampDownMs;
  let covered = 0;
  let elapsed = 0;
  while (elapsed < m.rampUpMs && covered < m.travelMm) {
    covered += speed * fraction(m, elapsed, m.travelMm - covered, zone) * STEP_MS;
    elapsed += STEP_MS;
  }
  if (covered >= m.travelMm) return elapsed;
  let remaining = m.travelMm - covered;
  let after = 0;
  if (remaining > zone) {
    after += (remaining - zone) / (speed * top);
    remaining = zone;
  }
  if (remaining > 0)
    after +=
      top - slow < 1e-6
        ? remaining / (speed * top)
        : (zone / (speed * (top - slow))) * Math.log((slow + ((top - slow) * remaining) / zone) / slow);
  return Math.ceil(elapsed + after);
};

/** The hard limits in seconds: a full move, and a move from an unknown position (at the slow duty). */
export const moveLimits = (m: Motion): { fullSeconds: number; unknownSeconds: number } => {
  const slowSpeed = (m.speedMmPerSecond / 1000) * duty(m.slowDutyPercent);
  return {
    fullSeconds: expectedMoveMs(m) / 1000 + m.timeoutMarginSeconds,
    unknownSeconds: slowSpeed > 0 ? Math.ceil(m.travelMm / slowSpeed) / 1000 + m.timeoutMarginSeconds : m.timeoutMarginSeconds,
  };
};
