import { describe, expect, it } from 'vitest';
import { bypassedChecks, closePhaseBlocker, moveBlockers, releaseOffered } from '../moveBlockers';
import { mockStatus } from '../../mocks/data';
import { formatTime } from '../../i18n/format';
import type { RoofStatus } from '../../types';

// Spec 026: one explanation of why the roof can't move, for every page that refuses or holds a move.

const healthy = { weather: 'safe', parkConfirmed: true, fault: null, closePhase: 'idle', supplyLow: false, state: 'closed' } as const;
const roof = (change: Partial<RoofStatus> = {}): RoofStatus => ({ ...structuredClone(mockStatus.roof!), ...healthy, ...change });
const ctx = { live: true, rearmMinutes: 10, nowMs: Date.UTC(2026, 10, 24, 0, 15) };
const ids = (r: RoofStatus, action: 'open' | 'close' | 'move' = 'move') => moveBlockers(r, action, ctx).map((b) => b.id);
const testBox = { set: true, active: true, park: false, weather: false, daylight: false, until: 1, expired: false, waitingForClock: false, parkRequests: false, text: '' };

describe('moveBlockers', () => {
  it('nothing stops a move on a healthy roof at rest', () => {
    expect(ids(roof())).toEqual([]);
  });

  it('no live data is the only blocker without live data', () => {
    expect(moveBlockers(roof({ weather: 'unsafe' }), 'move', { ...ctx, live: false }).map((b) => b.id)).toEqual(['noLiveData']);
  });

  it.each([
    ['weather', { weather: 'unsafe', weatherText: 'unsafe: rain' }],
    ['park', { parkConfirmed: false, parkText: 'N.I.N.A. reports not parked' }],
    ['waitPark', { closePhase: 'waitPark' }],
    ['heldByPerson', { closePhase: 'heldByPerson' }],
    ['halted', { closePhase: 'halted' }],
    ['moving', { state: 'opening' }],
    ['closeWaiting', { closePending: true }],
    ['calibrating', { calibrating: true }],
    ['supply', { supplyLow: true, supplyVolts: 10.2 }],
    ['hall', { hall: { counts: 0, level: 'rail' } }],
    ['hall', { hall: { counts: 1200, level: 'unsettled' } }],
  ] as [string, Partial<RoofStatus>][])('names %s for an attended move', (id, change) => {
    expect(ids(roof(change))).toEqual([id]);
  });

  it('each blocker says what stops it, what it means, when it clears; settings blockers link to their card', () => {
    const [weather] = moveBlockers(roof({ weather: 'unsafe', weatherText: 'unsafe: rain' }), 'move', ctx);
    expect(weather.title).toBe('Weather check: unsafe: rain');
    expect(weather.meaning).toMatch(/only while the weather check reads safe/);
    expect(weather.clears).toBe('Clears once SQMeter reports safe.');
    expect(weather.fix).toEqual({ label: 'Weather check settings', href: '/settings?tab=roof&section=safety' });
    const [park] = moveBlockers(roof({ parkConfirmed: false, parkText: 'N.I.N.A. reports not parked' }), 'close', ctx);
    expect(park.title).toBe('Park check: N.I.N.A. reports not parked');
    expect(park.fix?.href).toBe('/settings?tab=roof&section=park');
  });

  it('Open is held by the weather and daylight, Close by the park', () => {
    const daylight = { mode: 'close', state: 'day', sunAltitudeDeg: 3, limitDeg: 0, text: 'Sun 3.0° ≥ 0.0°', closes: true } as const;
    const r = roof({ weather: 'unsafe', parkConfirmed: false, daylight });
    expect(ids(r, 'open')).toEqual(['weather', 'daylight']);
    expect(ids(r, 'close')).toEqual(['park']);
    expect(ids(r, 'move')).toEqual(['weather', 'daylight', 'park']);
  });

  it('a fault comes first, with Clear fault; a fault covers the automatic close that can\'t move', () => {
    const fault = { kind: 'didNotLeave', cause: 'logic', measured: 10, limit: 10, text: 'Still on the start magnet after 10.0 s' } as const;
    const list = moveBlockers(roof({ fault, closePhase: 'cannotMove', weather: 'unsafe' }), 'move', ctx);
    expect(list.map((b) => b.id)).toEqual(['fault', 'weather']);
    expect(list[0].fix).toMatchObject({ act: 'clearFault' });
  });

  it('a move that timed out says its limit comes from travel and speed, and to run the calibration run', () => {
    const fault = { kind: 'timeout', cause: 'logic', measured: 63400, limit: 63400, text: 'No target magnet after 63.4 s >= 63.4 s timeout' } as const;
    const [blocker] = moveBlockers(roof({ fault }), 'move', ctx);
    expect(blocker.meaning).toMatch(/travel, speed and the margin/);
    expect(blocker.meaning).toMatch(/calibration run/);
  });

  it('a pin setup fault links to the Pins page instead of Clear fault', () => {
    const fault = { kind: 'external', cause: 'pinSetup', measured: null, limit: null, text: 'No board chosen yet' } as const;
    expect(moveBlockers(roof({ fault }), 'open', ctx)[0].fix).toEqual({ label: 'Fix the pins', href: '/pins' });
  });

  it('jog, teach and the calibration run wait for the automatic close; Open and Close name it elsewhere', () => {
    expect(ids(roof({ closePhase: 'heldByPerson' }), 'open')).toEqual([]);
  });
});

describe('the automatic close on hold', () => {
  it('says when it clears from the device countdown, and offers Release hold only while safe', () => {
    const held = roof({ closePhase: 'heldByPerson', closeRearmSeconds: 600 });
    const blocker = closePhaseBlocker(held, ctx)!;
    expect(blocker.title).toBe('Automatic close on hold after a Stop');
    expect(blocker.clears).toBe(`Clears at ${formatTime(new Date(ctx.nowMs + 600_000))} if the weather stays safe, or once the roof is closed.`);
    expect(blocker.fix).toMatchObject({ act: 'releaseHold', label: 'Release hold' });
    expect(releaseOffered(held)).toBe(true);
  });

  it('while unsafe: no countdown and no release, only "after 10 min of safe weather"', () => {
    const held = roof({ closePhase: 'halted', closeRearmSeconds: null, weather: 'unsafe' });
    const blocker = closePhaseBlocker(held, ctx)!;
    expect(blocker.clears).toBe('Clears after 10 min of safe weather, or once the roof is closed.');
    expect(blocker.fix).toBeUndefined();
    expect(releaseOffered(held)).toBe(false);
  });

  it('no release while daylight wants the roof closed', () => {
    const daylight = { mode: 'close', state: 'day', sunAltitudeDeg: 3, limitDeg: 0, text: 'Sun up', closes: true } as const;
    expect(releaseOffered(roof({ closePhase: 'heldByPerson', daylight }))).toBe(false);
  });

  it('idle is not a blocker', () => {
    expect(closePhaseBlocker(roof(), ctx)).toBeNull();
  });
});

describe('bypassedChecks', () => {
  it('names each check a test box bypass stands in for now, never as passing', () => {
    expect(bypassedChecks(roof({ testBox: { ...testBox, park: true, weather: true } }))).toEqual(['Park check', 'Weather check']);
    expect(bypassedChecks(roof({ testBox: { ...testBox, active: false, park: true } }))).toEqual([]);
  });
});
