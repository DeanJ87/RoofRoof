import { describe, expect, it } from 'vitest';
import { mockStatus } from '../../mocks/data';
import type { RoofState, RoofStatus, SystemStatus } from '../../types';
import {
  RESTART_HOLD_MS,
  clearFaultReason,
  RESULT_MS,
  closeButton,
  closePhasePill,
  hallMarks,
  openButton,
  positionOf,
  resultNote,
  roofView,
  statePill,
  stopBarShown,
  type CommandResult,
  type LiveState,
  type ViewInput,
} from '../roofView';

// Spec 009 FR-003 to FR-008, FR-012 to FR-014: what the Roof card offers in
// every state. The device decides; the page only greys a button with its reason.

const NOW = 1_000_000;

const status = (roof: Partial<RoofStatus> = {}, apMode = false): SystemStatus => {
  const s = structuredClone(mockStatus);
  s.roof = { ...s.roof!, ...roof };
  s.wifi = { ...s.wifi, apMode };
  return s;
};

const live = (s: SystemStatus | null = status(), over: Partial<LiveState> = {}): LiveState => ({
  status: s,
  connected: true,
  quiet: false,
  reconnectedAt: null,
  ...over,
});

const input = (over: Partial<ViewInput> = {}): ViewInput => ({ live: live(), result: null, inFlight: null, now: NOW, ...over });

const refused = (reason: string, over: Partial<CommandResult> = {}): CommandResult => ({
  command: 'open',
  outcome: 'refused',
  reason,
  text: `device says ${reason}`,
  at: NOW - 1000,
  stateAt: 'closed',
  ...over,
});

// Every combination of the given value lists.
type Lists = readonly (readonly unknown[])[];
type Combination<T extends Lists> = { [K in keyof T]: T[K][number] };
const product = <T extends Lists>(lists: T): Combination<T>[] =>
  lists.reduce<unknown[][]>((acc, list) => acc.flatMap((head) => list.map((value) => [...head, value])), [[]]) as Combination<T>[];

const FAULT = { kind: 'timeout', cause: 'logic' as const, measured: 61, limit: 60, text: 'No target magnet after 61 s >= 60 s timeout' };

describe('Stop', () => {
  it('is enabled in every combination the card can be in', () => {
    const states: RoofState[] = ['unknown', 'closed', 'open', 'opening', 'closing', 'stopped', 'error'];
    const cases = product([
      states,
      [null, FAULT],
      ['safe', 'unsafe', 'unknown'],
      [true, false], // park confirmed
      [true, false], // supply low
      [true, false], // close pending
      [true, false], // hotspot
      [
        [true, false],
        [false, false],
        [true, true],
      ], // connected, quiet
      [null, refused('busy'), refused('restarting'), refused('hotspot')],
      [null, 'open', 'stop'],
    ] as const);
    for (const [state, fault, weather, parkConfirmed, supplyLow, closePending, apMode, [connected, quiet], result, inFlight] of cases) {
      const s = status({ state, fault, weather, parkConfirmed, supplyLow, closePending }, apMode);
      const view = roofView(input({ live: live(s, { connected, quiet }), result, inFlight }));
      expect(view.stop.disabled).toBe(false);
      // A greyed-out button never appears without its reason (constitution VII).
      for (const b of [view.open, view.close]) expect(!b.disabled || b.busy || b.reason !== '').toBe(true);
    }
  });

  it('is enabled with no status at all', () => {
    expect(roofView(input({ live: live(null, { connected: false }) })).stop.disabled).toBe(false);
  });
});

describe('Open and Close need live data', () => {
  it.each([
    ['no status yet', live(null)],
    ['the connection is down', live(status(), { connected: false })],
    ['the data has gone quiet', live(status(), { quiet: true })],
  ])('both are disabled when %s', (_, state) => {
    const view = roofView(input({ live: state }));
    expect(view.open).toMatchObject({ disabled: true, reason: 'No live data' });
    expect(view.close).toMatchObject({ disabled: true, reason: 'No live data' });
  });

  it('both are disabled when the roof is not reporting', () => {
    const s = status();
    delete s.roof;
    expect(openButton(input({ live: live(s) })).reason).toBe("The roof isn't reporting");
  });
});

describe('Open', () => {
  const open = (roof: Partial<RoofStatus>, apMode = false) =>
    openButton(input({ live: live(status({ state: 'closed', ...roof }, apMode)) }));

  it('is enabled for a closed roof in safe weather', () => {
    expect(open({})).toEqual({ disabled: false, reason: '', busy: false });
  });

  it('needs the weather to be safe, and says why', () => {
    expect(open({ weather: 'unsafe', weatherText: 'unsafe: rain (SQMeter HTTP)' }).reason).toContain('unsafe: rain (SQMeter HTTP)');
    expect(open({ weather: 'unknown', weatherText: '' }).disabled).toBe(true);
  });

  it('is refused while the setup hotspot is on, Close is not', () => {
    expect(open({}, true).reason).toBe("Opening isn't available while the setup hotspot is on");
    const s = status({ state: 'open' }, true);
    expect(closeButton(input({ live: live(s) })).disabled).toBe(false);
  });

  it('is disabled when open or opening', () => {
    expect(open({ state: 'open' }).reason).toBe('Already open');
    expect(open({ state: 'opening' }).reason).toBe('Already opening');
  });

  it('is disabled with a fault before anything else the roof says', () => {
    expect(open({ fault: FAULT, weather: 'unsafe' }).reason).toBe('Fault: clear it first');
  });

  it('states the supply and its limit', () => {
    expect(open({ supplyLow: true, supplyVolts: 10.9, minSupplyVolts: 11.5 }).reason).toBe('Supply 10.9 V is below 11.5 V');
    expect(open({ supplyLow: true, supplyVolts: null, minSupplyVolts: 11.5 }).reason).toBe('Supply unreadable (minimum 11.5 V)');
  });

  it('waits for the Hall reading to settle', () => {
    expect(open({ hall: { level: 'unsettled', counts: 0 } }).reason).toBe('Hall reading not settled');
  });

  it('stays usable from an unknown position: a person may move it, slowly', () => {
    expect(open({ state: 'unknown', positionKnown: false, positionPercent: null }).disabled).toBe(false);
  });
});

describe('Close', () => {
  const close = (roof: Partial<RoofStatus>) => closeButton(input({ live: live(status({ state: 'open', ...roof })) }));

  it('is enabled for an open roof over a parked mount, whatever the weather', () => {
    expect(close({ weather: 'unsafe' })).toEqual({ disabled: false, reason: '', busy: false });
  });

  it('needs the park confirmed, with the device’s reason', () => {
    expect(close({ parkConfirmed: false, parkText: 'Mount not parked: Park switch 1 open' }).reason).toBe(
      'Mount not parked: Park switch 1 open',
    );
    expect(close({ parkConfirmed: false, parkText: '' }).reason).toBe('Mount not parked');
  });

  it('is disabled when closed, closing or a close waits', () => {
    expect(close({ state: 'closed' }).reason).toBe('Already closed');
    expect(close({ state: 'closing' }).reason).toBe('Already closing');
    expect(close({ closePending: true }).reason).toBe('A close is waiting to start');
  });

  it('can reverse an opening roof', () => {
    expect(close({ state: 'opening' }).disabled).toBe(false);
  });
});

describe('a command in flight', () => {
  it('makes Open and Close busy, never Stop', () => {
    const view = roofView(input({ live: live(status({ state: 'stopped' })), inFlight: 'open' }));
    expect(view.open).toEqual({ disabled: true, reason: '', busy: true });
    expect(view.close.busy).toBe(true);
    expect(view.stop.disabled).toBe(false);
  });

  it('a stop in flight leaves Open and Close as they were', () => {
    expect(openButton(input({ live: live(status({ state: 'stopped' })), inFlight: 'stop' })).disabled).toBe(false);
  });
});

describe('refusals', () => {
  const stopped = live(status({ state: 'stopped' }));

  it('busy: shown, and the buttons stay usable', () => {
    const result = refused('busy', { stateAt: 'stopped' });
    const view = roofView(input({ live: stopped, result }));
    expect(view.open.disabled).toBe(false);
    expect(view.result).toEqual({ id: 'result', tone: 'bad', text: 'device says busy' });
  });

  it('restarting: Open and Close held until live data is back after a reconnect', () => {
    const result = refused('restarting', { stateAt: 'stopped' });
    expect(roofView(input({ live: stopped, result })).close.reason).toBe('The device is restarting');
    const back = live(status({ state: 'stopped' }), { reconnectedAt: NOW - 500 });
    expect(roofView(input({ live: back, result })).close.disabled).toBe(false);
  });

  it('restarting: the hold ends after 30 s', () => {
    const result = refused('restarting', { at: NOW - RESTART_HOLD_MS, stateAt: 'stopped' });
    expect(roofView(input({ live: stopped, result })).open.disabled).toBe(false);
  });

  it('hotspot: Open held while the answer is shown', () => {
    const result = refused('hotspot', { stateAt: 'stopped' });
    expect(openButton(input({ live: stopped, result })).reason).toBe("Opening isn't available while the setup hotspot is on");
    expect(closeButton(input({ live: stopped, result })).disabled).toBe(false);
  });

  it('an answer is dropped when the roof changes state or after 15 s', () => {
    const roof = status({ state: 'opening' }).roof!;
    expect(resultNote(refused('busy', { stateAt: 'closed' }), roof, NOW)).toBeNull();
    const old = refused('busy', { at: NOW - RESULT_MS, stateAt: 'opening' });
    expect(resultNote(old, roof, NOW)).toBeNull();
  });

  it('an accepted command needs no note; a waiting close and an unreachable device do', () => {
    const roof = status({ state: 'open' }).roof!;
    const base = { command: 'close' as const, reason: '', at: NOW, stateAt: 'open' as const };
    expect(resultNote({ ...base, outcome: 'accepted', text: '' }, roof, NOW)).toBeNull();
    expect(resultNote({ ...base, outcome: 'pending', text: 'Close starts once this has stopped' }, roof, NOW)?.tone).toBe('muted');
    expect(resultNote({ ...base, outcome: 'unreachable', text: 'x' }, roof, NOW)?.tone).toBe('bad');
  });
});

describe('notes', () => {
  it('says a move from an unknown position goes slowly to the first magnet', () => {
    const view = roofView(input({ live: live(status({ state: 'unknown', positionKnown: false, positionPercent: null })) }));
    expect(view.notes.map((n) => n.id)).toContain('unknown-position');
  });

  it.each(['waitPark', 'closing', 'retryWait'] as const)('warns that Stop calls off the automatic close (%s)', (closePhase) => {
    const view = roofView(input({ live: live(status({ closePhase })) }));
    expect(view.notes).toContainEqual({ id: 'auto-close', tone: 'warn', text: 'Stop calls off the automatic close' });
  });

  it('has nothing to say about a healthy closed roof', () => {
    expect(roofView(input()).notes).toEqual([]);
  });
});

describe('pills and position', () => {
  it('names the state, and a fault before it', () => {
    expect(statePill(status({ state: 'closed' }).roof!)).toMatchObject({ label: 'Closed', tone: 'pill-green' });
    expect(statePill(status({ state: 'error', fault: FAULT }).roof!)).toMatchObject({ label: 'Fault', tone: 'pill-red' });
    expect(statePill(null).label).toBe('Not reporting');
  });

  it('gives the percentage open, clamped, or Unknown', () => {
    expect(positionOf(status({ positionPercent: 45.4 }).roof!)).toEqual({ percent: 45, text: '45% open' });
    expect(positionOf(status({ positionPercent: 104 }).roof!).percent).toBe(100);
    expect(positionOf(status({ positionKnown: false, positionPercent: null }).roof!)).toEqual({ percent: null, text: 'Unknown' });
  });

  it('shows the automatic close only when it is not idle', () => {
    expect(closePhasePill('idle')).toBeNull();
    expect(closePhasePill('halted')?.tone).toBe('pill-red');
    expect(closePhasePill('heldByPerson')?.tone).toBe('pill-amber');
  });
});

describe('the Stop bar', () => {
  it.each([
    [{ state: 'opening' }, true],
    [{ state: 'closing' }, true],
    [{ state: 'open', closePending: true }, true],
    [{ state: 'closed' }, false],
    [{ state: 'stopped' }, false],
  ] as [Partial<RoofStatus>, boolean][])('%o: %s', (roof, shown) => {
    expect(stopBarShown(status(roof))).toBe(shown);
  });

  it('is not shown without a status at first, and is once none has come for 5 s', () => {
    expect(stopBarShown(null)).toBe(false);
    expect(stopBarShown(null, 5000)).toBe(true);
  });
});

describe('the Hall scale', () => {
  it('places the reading and the thresholds on 0-4095, clamped', () => {
    expect(hallMarks(4095, 1000, 3000)).toEqual({ reading: 1, low: 1000 / 4095, high: 3000 / 4095 });
    expect(hallMarks(-5).reading).toBe(0);
    expect(hallMarks(5000).reading).toBe(1);
  });

  it('leaves out thresholds it does not know', () => {
    expect(hallMarks(700)).toMatchObject({ low: null, high: null });
  });
});

// Review fixes: a page that slept, an unconfirmed Stop, Clear fault.
describe('live data and results over time', () => {
  it('counts the data stale from its last message, even if the quiet tick lags', () => {
    const slept = live(status(), { lastMessageAt: NOW - 6000 });
    expect(openButton(input({ live: slept })).reason).toBe('No live data');
    expect(openButton(input({ live: live(status(), { lastMessageAt: NOW - 1000 }) })).disabled).toBe(false);
  });

  it('keeps an unconfirmed Stop on screen while the roof is still seen moving, however long', () => {
    const failed: CommandResult = {
      command: 'stop',
      outcome: 'unreachable',
      reason: '',
      text: 'Stop not confirmed',
      at: 0,
      stateAt: 'opening',
    };
    expect(resultNote(failed, status({ state: 'opening' }).roof!, NOW)?.text).toBe('Stop not confirmed');
    expect(resultNote(failed, null, NOW)).not.toBeNull();
    expect(resultNote(failed, status({ state: 'stopped' }).roof!, NOW)).toBeNull();
  });

  it('a fault being cleared keeps Open and Close disabled with a reason, not busy', () => {
    expect(openButton(input({ live: live(status({ state: 'stopped' })), inFlight: 'clear-fault' }))).toEqual({
      disabled: true,
      reason: 'Clearing the fault',
      busy: false,
    });
  });

  it('Clear fault needs live data', () => {
    expect(clearFaultReason(input({ live: live(status(), { connected: false }) }))).toBe('No live data');
    expect(clearFaultReason(input())).toBe('');
  });
});
