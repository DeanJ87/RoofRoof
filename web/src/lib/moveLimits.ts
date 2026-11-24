import type { RoofConfig } from '../types';

// Spec 026: a move's hard time limit as the device works it out
// (lib/RoofLogic/src/MotionProfile.cpp: moveTimeoutMs and unknownMoveTimeoutMs, with the
// measured slow speed), so Settings can show it beside travel and speed as they are typed.
// Display only: the motion code enforces its own (constitution I).

type Motion = Pick<
  RoofConfig,
  | 'travelMm'
  | 'speedMmPerSecond'
  | 'timeoutMarginSeconds'
  | 'rampUpMs'
  | 'rampDownMs'
  | 'topDutyPercent'
  | 'slowDutyPercent'
  | 'slowSpeedMmPerSecond'
  | 'slowSpeedDutyPercent'
>;

const STEP_MS = 10; // the device integrates the ramp-up in 10 ms steps
const duty = (percent: number) => Math.min(percent, 100) / 100;

// mm per ms at the slow duty: the measured slow speed while it belongs to that duty, else proportional.
const slowSpeed = (m: Motion) => {
  const full = m.speedMmPerSecond / 1000;
  if ((m.slowSpeedMmPerSecond ?? 0) > 0 && m.slowSpeedDutyPercent === m.slowDutyPercent)
    return Math.min(full, (m.slowSpeedMmPerSecond ?? 0) / 1000);
  return full * duty(m.slowDutyPercent);
};

// mm per ms at duty `fraction`: affine between the slow duty's speed and the full speed.
const speedAt = (m: Motion, fraction: number) => {
  const slow = duty(m.slowDutyPercent);
  const atSlow = slowSpeed(m);
  if (fraction <= slow || slow >= 1) return slow > 0 ? (atSlow * Math.min(fraction, slow)) / slow : 0;
  return atSlow + ((m.speedMmPerSecond / 1000 - atSlow) * (fraction - slow)) / (1 - slow);
};

const zoneOf = (m: Motion) => speedAt(m, duty(m.topDutyPercent)) * m.rampDownMs;

// The duty fraction while ramping up and slowing down near the end (profileFraction).
const fraction = (m: Motion, drivenMs: number, remainingMm: number) => {
  const top = duty(m.topDutyPercent);
  const slow = duty(m.slowDutyPercent);
  if (!(remainingMm > 0)) return slow;
  const zone = zoneOf(m);
  const up = drivenMs < m.rampUpMs ? slow + ((top - slow) * drivenMs) / m.rampUpMs : top;
  const down = zone > 0 && remainingMm < zone ? slow + ((top - slow) * remainingMm) / zone : top;
  return Math.max(slow, Math.min(up, down));
};

// Once the ramp-up is over: cruise at the top duty to the zone, then an exponential approach to the slow speed.
const afterRampUp = (m: Motion, remainingMm: number) => {
  const atTop = speedAt(m, duty(m.topDutyPercent));
  const atSlow = slowSpeed(m);
  const zone = zoneOf(m);
  let ms = 0;
  let left = remainingMm;
  if (left > zone) {
    ms += (left - zone) / atTop;
    left = zone;
  }
  if (left <= 0) return ms;
  if (atTop - atSlow < 1e-9) return ms + left / atSlow;
  return ms + (zone / (atTop - atSlow)) * Math.log((atSlow + ((atTop - atSlow) * left) / zone) / atSlow);
};

/** The expected time of a full move, in ms (0 for settings the device wouldn't accept). */
export const expectedMoveMs = (m: Motion): number => {
  if (!(m.travelMm > 0) || !(m.speedMmPerSecond > 0) || !(duty(m.slowDutyPercent) > 0) || m.topDutyPercent < m.slowDutyPercent) return 0;
  let covered = 0;
  let elapsed = 0;
  while (elapsed < m.rampUpMs && covered < m.travelMm) {
    covered += speedAt(m, fraction(m, elapsed, m.travelMm - covered)) * STEP_MS;
    elapsed += STEP_MS;
  }
  if (covered >= m.travelMm) return elapsed;
  return Math.ceil(elapsed + afterRampUp(m, m.travelMm - covered));
};

/** The hard limits in seconds: a full move, and a move from an unknown position (at the slow duty). */
export const moveLimits = (m: Motion): { fullSeconds: number; unknownSeconds: number } => {
  const slow = slowSpeed(m);
  return {
    fullSeconds: expectedMoveMs(m) / 1000 + m.timeoutMarginSeconds,
    unknownSeconds: slow > 0 ? Math.ceil(m.travelMm / slow) / 1000 + m.timeoutMarginSeconds : m.timeoutMarginSeconds,
  };
};
