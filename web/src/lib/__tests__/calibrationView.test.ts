import { describe, expect, it } from 'vitest';
import { acceptedRoof, proposalRows, settingsChangedSince, startBlocked, whereFromState } from '../calibrationView';
import { mockCalibration, mockConfig, mockStatus } from '../../mocks/data';
import type { RoofStatus } from '../../types';

// Spec 016: what the calibration page decides on its own (the device checks again).

const roof = (change: Partial<RoofStatus> = {}): RoofStatus => ({ ...structuredClone(mockStatus.roof!), ...change });

describe('startBlocked', () => {
  it('the first blocker, then the confirmation', () => {
    expect(startBlocked('Weather check: unsafe (rain)', true)).toBe('Weather check: unsafe (rain)');
    expect(startBlocked('', false)).toMatch(/at the roof/);
    expect(startBlocked('', true)).toBe('');
  });
});

describe('whereFromState', () => {
  it('guesses from the Hall level and the state', () => {
    expect(whereFromState(roof({ state: 'closed', hall: { counts: 700, level: 'low' } }))).toBe('closed');
    expect(whereFromState(roof({ state: 'open', hall: { counts: 3400, level: 'high' } }))).toBe('open');
    expect(whereFromState(roof({ state: 'unknown', hall: { counts: 2050, level: 'between' } }))).toBe('partWay');
    expect(whereFromState(undefined)).toBe('partWay');
  });
});

describe('the proposal', () => {
  const config = mockConfig.roof!;
  const proposal = mockCalibration.proposal!;

  it('shows each value beside the saved one', () => {
    const rows = proposalRows(proposal, { ...config, speedMmPerSecond: 38 });
    const speed = rows.find((row) => row.key === 'speedMmPerSecond')!;
    expect(speed).toMatchObject({ saved: '38', proposed: '41', changed: true });
    expect(rows.find((row) => row.key === 'hallHighEnd')!.proposed).toBe('Open');
  });

  it('marks a withheld value', () => {
    const rows = proposalRows({ ...proposal, hallHighCounts: null }, config);
    expect(rows.find((row) => row.key === 'hallHighCounts')!.proposed).toBeNull();
  });

  it('accepts every proposed value and nothing withheld', () => {
    const next = acceptedRoof({ ...proposal, hallLowCounts: null }, { ...config, hallLowCounts: 1234 });
    expect(next.speedMmPerSecond).toBe(41);
    expect(next.timeoutMarginSeconds).toBe(10);
    expect(next.hallLowCounts).toBe(1234);
    expect(next.leaveMagnetSeconds).toBe(config.leaveMagnetSeconds);
  });

  it('notices settings changed after the run started', () => {
    expect(settingsChangedSince(proposal, { ...config, speedMmPerSecond: 38, timeoutMarginSeconds: 15 })).toBe(false);
    expect(settingsChangedSince(proposal, { ...config, speedMmPerSecond: 39, timeoutMarginSeconds: 15 })).toBe(true);
  });
});
