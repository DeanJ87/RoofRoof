import { describe, expect, it } from 'vitest';
import { acceptedRoof, calibrationChecks, proposalRows, settingsChangedSince, startBlocked, whereFromState } from '../calibrationView';
import { mockCalibration, mockConfig, mockStatus } from '../../mocks/data';
import type { RoofStatus } from '../../types';

// Spec 016: what the calibration page decides on its own (the device checks again).

const roof = (change: Partial<RoofStatus> = {}): RoofStatus => ({ ...structuredClone(mockStatus.roof!), ...change });

describe('calibrationChecks', () => {
  it('passes on a healthy roof at rest', () => {
    const checks = calibrationChecks(roof({ weather: 'safe', parkConfirmed: true, fault: null, closePhase: 'idle', supplyLow: false }));
    expect(checks.every((check) => check.ok)).toBe(true);
  });

  it.each([
    ['weather', { weather: 'unsafe', weatherText: 'unsafe: rain' }],
    ['park', { parkConfirmed: false }],
    ['sequence', { closePhase: 'waitPark' }],
    ['still', { state: 'opening' }],
    ['still', { closePending: true }],
    ['supply', { supplyLow: true }],
  ] as [string, Partial<RoofStatus>][])('fails %s', (id, change) => {
    const base = roof({ weather: 'safe', parkConfirmed: true, fault: null, closePhase: 'idle', supplyLow: false });
    const failed = calibrationChecks({ ...base, ...change }).filter((check) => !check.ok);
    expect(failed.map((check) => check.id)).toEqual([id]);
  });

  it('fails the weather while daylight wants the roof closed', () => {
    const daylight = { mode: 'close', state: 'day', sunAltitudeDeg: 3, limitDeg: 0, text: 'Sun 3.0° ≥ 0.0°', closes: true } as const;
    const check = calibrationChecks(roof({ weather: 'safe', daylight })).find((c) => c.id === 'weather')!;
    expect(check.ok).toBe(false);
    expect(check.text).toContain('Sun');
  });

  it('fails an unsettled or railed Hall reading', () => {
    for (const level of ['rail', 'unsettled'] as const) {
      const check = calibrationChecks(roof({ hall: { counts: 0, level } })).find((c) => c.id === 'hall')!;
      expect(check.ok).toBe(false);
    }
  });
});

describe('startBlocked', () => {
  const ok = calibrationChecks(roof({ weather: 'safe', parkConfirmed: true, fault: null, closePhase: 'idle', supplyLow: false }));
  it('needs live data, every check and the confirmation', () => {
    expect(startBlocked(ok, true, false)).toMatch(/live data/);
    expect(startBlocked(ok, false, true)).toMatch(/at the roof/);
    expect(startBlocked(ok, true, true)).toBe('');
    const failing = calibrationChecks(roof({ weather: 'unsafe', parkConfirmed: true, fault: null, closePhase: 'idle' }));
    expect(startBlocked(failing, true, true)).toMatch(/Weather safe/);
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
