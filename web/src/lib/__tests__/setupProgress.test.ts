import { describe, expect, it } from 'vitest';
import { setupComplete, setupStages, valueSource } from '../setupProgress';
import { acceptedRoof } from '../calibrationView';
import { mockCalibration, mockConfig } from '../../mocks/data';
import type { Config } from '../../types';

// Spec 026: the roof's setup stages, in order, from the saved settings.
const config = (change: (c: Config) => void = () => undefined): Config => {
  const c = structuredClone(mockConfig);
  c.roof = { ...c.roof!, travelMm: 1850, speedMmPerSecond: 38, measured: 0 };
  change(c);
  return c;
};
const state = (c: Config, id: string) => setupStages(c).find((s) => s.id === id)?.state;

describe('setupStages', () => {
  it('lists the stages in an installer\'s order', () => {
    expect(setupStages(config()).map((s) => s.id)).toEqual(['board', 'weather', 'park', 'direction', 'ends', 'calibrated', 'alerts']);
  });
  it('before the calibration run says the move time limits are still defaults', () => {
    const stage = setupStages(config()).find((s) => s.id === 'calibrated')!;
    expect(stage.state).toBe('todo');
    expect(stage.note).toMatch(/^Move time limits are still defaults \(about 66 s a move\)/);
  });
  it('typed travel and speed are not measured; measured ones are done', () => {
    expect(state(config((c) => (c.roof!.travelMm = 2400)), 'calibrated')).toBe('typed');
    expect(state(config((c) => (c.roof!.measured = 3)), 'calibrated')).toBe('done');
    expect(state(config((c) => (c.roof!.measured = 8)), 'ends')).toBe('done');
    expect(state(config((c) => (c.roof!.measured = 16)), 'direction')).toBe('done');
  });
  it('a test box bypass is shown as bypassed, never done', () => {
    const c = config((x) => {
      x.testBox = { ...x.testBox!, weather: true, park: true };
    });
    expect(state(c, 'weather')).toBe('bypassed');
    expect(state(c, 'park')).toBe('bypassed');
    expect(setupComplete(setupStages(c))).toBe(false);
  });
  it('who set a value: measured, the default or typed', () => {
    const roof = config().roof!;
    expect(valueSource({ ...roof, measured: 3 }, 3, true)).toBe('measured');
    expect(valueSource(roof, 3, true)).toBe('default');
    expect(valueSource(roof, 3, false)).toBe('typed');
  });
});

describe('acceptedRoof marks what the run measured', () => {
  it('sets the bit of each proposed value and the direction', () => {
    const roof = config().roof!;
    const next = acceptedRoof(mockCalibration.proposal!, roof);
    expect((next.measured ?? 0) & 16).toBe(16);
    expect((next.measured ?? 0) & 1).toBe(mockCalibration.proposal!.travelMm ? 1 : 0);
  });
});
