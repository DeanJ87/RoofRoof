import { describe, expect, it } from 'vitest';
import { movePlan } from '../movePlan';
import { healthText, ninaLine, sqmeterLine } from '../sourceHealth';
import { mockStatus } from '../../mocks/data';
import type { RoofStatus } from '../../types';

// Spec 027 (FR-007, FR-009, FR-011, FR-015): what a press of a blocked Open or Close offers, and the
// sources' health lines.

const roof = (change: Partial<RoofStatus> = {}): RoofStatus => ({ ...structuredClone(mockStatus.roof!), ...change });
const ctx = { live: true, rearmMinutes: 10, nowMs: 0 };
const daylight = { mode: 'close', state: 'day', sunAltitudeDeg: 3, limitDeg: 0, text: 'Sun 3.0° ≥ 0.0°', closes: true } as const;
const cloud = { weather: 'unsafe', weatherText: 'unsafe: cloud' } as const;

describe('movePlan', () => {
  it('nothing blocks: the command just goes', () => {
    expect(movePlan(roof(), 'open', ctx)).toBeNull();
    expect(movePlan(roof({ state: 'open' }), 'close', ctx)).toBeNull();
  });

  it('only the park blocks an Open: the dialog may wait for it or open anyway (spec 039)', () => {
    const plan = movePlan(roof({ parkConfirmed: false, openOverridable: ['mountNotParked'] }), 'open', ctx)!;
    expect(plan.override).toEqual(['mountNotParked']);
    expect(plan.parkOnly).toBe(true);
    // A device that doesn't offer the park: nothing to override.
    expect(movePlan(roof({ parkConfirmed: false, openOverridable: [] }), 'open', ctx)!.override).toBeNull();
  });

  it('daylight, cloud and the park a person may override: Open anyway names each (spec 039: the park too)', () => {
    const plan = movePlan(
      roof({ ...cloud, daylight, parkConfirmed: false, openOverridable: ['mountNotParked', 'weatherNotSafe', 'daylight'] }),
      'open',
      ctx,
    )!;
    expect(plan.reasons.map((b) => b.id)).toEqual(['weather', 'daylight', 'park']);
    expect(plan.override).toEqual(['mountNotParked', 'weatherNotSafe', 'daylight']);
    expect(plan.park).toBe(true);
    expect(plan.parkOnly).toBe(false);
    // Only what is in the way is named.
    const day = movePlan(roof({ daylight, openOverridable: ['weatherNotSafe', 'daylight'] }), 'open', ctx)!;
    expect(day.override).toEqual(['daylight']);
  });

  it('rain, a lost source or the supply: every reason listed, no override', () => {
    const rain = movePlan(roof({ weather: 'unsafe', weatherText: 'unsafe: rain', daylight, openOverridable: ['daylight'] }), 'open', ctx)!;
    expect(rain.reasons.map((b) => b.id)).toEqual(['weather', 'daylight']);
    expect(rain.override).toBeNull();
    const supply = movePlan(roof({ daylight, supplyLow: true, openOverridable: ['daylight'] }), 'open', ctx)!;
    expect(supply.override).toBeNull();
  });

  it('a Close blocked only by the park offers Close anyway; with anything else, nothing', () => {
    const plan = movePlan(roof({ state: 'open', parkConfirmed: false }), 'close', ctx)!;
    expect(plan.override).toEqual(['mountNotParked']);
    const supply = movePlan(roof({ state: 'open', parkConfirmed: false, supplyLow: true }), 'close', ctx)!;
    expect(supply.override).toBeNull();
    expect(supply.reasons).toHaveLength(2);
  });
});

describe('source health', () => {
  it("SQMeter's last reply, or how long it has been silent and why", () => {
    expect(sqmeterLine(roof())).toEqual({ ok: true, text: 'SQMeter last replied 12 s ago' });
    const silent = roof({ sqmeter: { on: true, state: 'unreachable', lastReplySeconds: 75, text: 'no reply' } });
    expect(sqmeterLine(silent)).toEqual({ ok: false, text: 'No reply from SQMeter for 1:15', detail: 'no reply' });
    expect(healthText(sqmeterLine(silent)!)).toBe('No reply from SQMeter for 1:15: no reply');
    const never = roof({ sqmeter: { on: true, state: 'unreachable', lastReplySeconds: null, text: 'HTTP 404' } });
    expect(sqmeterLine(never)).toEqual({ ok: false, text: 'No reply from SQMeter yet', detail: 'HTTP 404' });
    expect(sqmeterLine(roof({ sqmeter: { on: false, state: 'off', lastReplySeconds: null, text: '' } }))).toBeNull();
  });

  it("N.I.N.A.'s reach, only with an address set", () => {
    expect(ninaLine(roof())).toEqual({ ok: true, text: 'N.I.N.A. answered 2 s ago' });
    const lost = roof({ nina: { ...mockStatus.roof!.nina!, reachable: false, state: 'unreachable', text: 'connection refused' } });
    expect(ninaLine(lost)).toEqual({ ok: false, text: 'N.I.N.A. not reachable', detail: 'connection refused' });
    expect(ninaLine(roof({ nina: { ...mockStatus.roof!.nina!, set: false } }))).toBeNull();
  });
});
