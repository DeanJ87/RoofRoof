import { describe, expect, it } from 'vitest';
import { expectedMoveMs, moveLimits } from '../moveLimits';

// Spec 026: the limits Settings shows beside travel and speed, as the device works them out
// (the demo, running the firmware's code, faults a default roof's jammed move at 65.9 s).
const defaults = {
  travelMm: 1850,
  speedMmPerSecond: 38,
  timeoutMarginSeconds: 15,
  rampUpMs: 2000,
  rampDownMs: 3000,
  topDutyPercent: 100,
  slowDutyPercent: 40,
  slowSpeedMmPerSecond: 0,
  slowSpeedDutyPercent: 0,
};

describe('moveLimits', () => {
  it('matches the device for the default roof', () => {
    expect(moveLimits(defaults).fullSeconds).toBeCloseTo(65.9, 1);
    expect(moveLimits(defaults).unknownSeconds).toBeCloseTo(1850 / 15.2 + 15, 0);
  });
  it('grows with the travel and shrinks with the speed', () => {
    expect(moveLimits({ ...defaults, speedMmPerSecond: 19 }).fullSeconds).toBeGreaterThan(100);
    expect(expectedMoveMs({ ...defaults, travelMm: 100 })).toBeLessThan(expectedMoveMs(defaults));
  });
  it('uses a measured slow speed for a move from an unknown position, only at the duty it was measured at', () => {
    const slow = { ...defaults, slowSpeedMmPerSecond: 10, slowSpeedDutyPercent: 40 };
    expect(moveLimits(slow).unknownSeconds).toBeCloseTo(1850 / 10 + 15, 0);
    expect(moveLimits({ ...slow, slowSpeedDutyPercent: 50 }).unknownSeconds).toBeCloseTo(moveLimits(defaults).unknownSeconds, 3);
  });
  it('is zero for settings the device would refuse', () => {
    expect(expectedMoveMs({ ...defaults, speedMmPerSecond: 0 })).toBe(0);
  });
});
