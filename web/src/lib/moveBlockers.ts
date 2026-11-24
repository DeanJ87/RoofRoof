import type { RoofStatus } from '../types';
import { t, type MessageKey } from '../i18n';
import { deviceText } from '../i18n/deviceMessage';
import { formatNumber, formatTime } from '../i18n/format';

// Spec 026: "Why can't the roof move?" in one place. Every page that refuses or
// holds a move (the dashboard, Calibrate's jog, teach and calibration run, the
// setup wizard's motor screens) lists the same blockers in the same words: what
// stops it, what that means, when it clears and the one thing that fixes it.
// The page only explains; the device decides every move (constitution I-III).

/** What the person wants to do: Open, Close, or an attended move (jog, teach, the calibration run). */
export type MoveAction = 'open' | 'close' | 'move';

/** The one thing that fixes a blocker: a page to go to, or an action on the device. */
export interface BlockerFix {
  label: string;
  href?: string; // a route, e.g. '/settings?tab=roof&section=park'
  act?: 'clearFault' | 'releaseHold';
}

export interface Blocker {
  id: string;
  tone: 'warn' | 'bad';
  title: string; // what stops it, with the value the device reports
  meaning: string; // what that means (behind "?")
  clears: string; // when it clears
  fix?: BlockerFix;
}

export interface BlockerContext {
  live: boolean; // the page has live data from the device
  rearmMinutes: number; // autoClose.rearmMinutes
  nowMs: number; // the browser's clock, for "clears at 00:25"
}

const make = (id: string, tone: Blocker['tone'], title: string, meaning: string, clears: string, fix?: BlockerFix): Blocker => ({
  id,
  tone,
  title,
  meaning,
  clears,
  fix,
});

const settings = (section: string, label: MessageKey): BlockerFix => ({ label: t(label), href: `/settings?tab=roof&section=${section}` });

const STAND_DOWN = ['heldByPerson', 'halted'];
const WORKING = ['waitPark', 'closing', 'retryWait'];

/** When a stand-down ends by itself: a clock time while it counts down, else after the re-arm spell. */
const standDownClears = (roof: RoofStatus, ctx: BlockerContext): string => {
  const seconds = roof.closeRearmSeconds;
  if (typeof seconds === 'number') return t('move.clears.at', { time: formatTime(new Date(ctx.nowMs + seconds * 1000)) });
  return t('move.clears.afterSafe', { minutes: ctx.rearmMinutes });
};

/** Release is offered only while the device would take it: stood down and nothing wants the roof closed. */
export const releaseOffered = (roof: RoofStatus): boolean =>
  STAND_DOWN.includes(roof.closePhase) && roof.weather === 'safe' && !roof.daylight?.closes;

/**
 * The automatic close as the person needs to know it, or null while it is idle: a stand-down (a person's
 * Stop, a close that took too long), the close at work, or unable to move.
 */
export const closePhaseBlocker = (roof: RoofStatus, ctx: BlockerContext): Blocker | null => {
  const phase = roof.closePhase;
  if (STAND_DOWN.includes(phase)) {
    const fix: BlockerFix | undefined = releaseOffered(roof) ? { label: t('move.fix.release'), act: 'releaseHold' } : undefined;
    const halted = phase === 'halted';
    return make(phase, 'warn', t(halted ? 'move.halted' : 'move.held'), t(halted ? 'move.haltedWhy' : 'move.heldWhy'), standDownClears(roof, ctx), fix);
  }
  if (WORKING.includes(phase))
    return make(phase, 'warn', t('move.working'), t(`roof.closePhase.${phase}Hint` as MessageKey), t('move.clears.working'));
  if (phase === 'cannotMove') return make(phase, 'bad', t('move.cannotMove'), t('roof.closePhase.cannotMoveHint'), t('move.clears.cannotMove'));
  return null;
};

export const faultBlocker = (roof: RoofStatus): Blocker | null => {
  const fault = roof.fault;
  if (!fault) return null;
  const title = deviceText(fault.text);
  if (fault.cause === 'pinSetup') return make('fault', 'bad', title, t('move.pinsWhy'), t('move.clears.pins'), { label: t('move.fix.pins'), href: '/pins' });
  // A move that ran out of time: its limit comes from travel and speed (spec 026).
  const meaning = fault.kind === 'timeout' ? t('move.timeoutWhy') : t('move.faultWhy');
  return make('fault', 'bad', title, meaning, t('move.clears.fault'), { label: t('move.fix.clearFault'), act: 'clearFault' });
};

const weatherBlocker = (roof: RoofStatus): Blocker | null => {
  if (roof.weather === 'safe') return null;
  const text = deviceText(roof.weatherText) || t('roof.weather.unknown');
  const unsafe = roof.weather === 'unsafe';
  return make(
    'weather',
    'warn',
    t(unsafe ? 'move.weatherUnsafe' : 'move.weatherUnknown', { text }),
    t('move.weatherWhy'),
    t(unsafe ? 'move.clears.weather' : 'move.clears.weatherUnknown'),
    settings('safety', 'move.fix.weather'),
  );
};

const daylightBlocker = (roof: RoofStatus): Blocker | null =>
  roof.daylight?.closes
    ? make(
        'daylight',
        'warn',
        t('move.daylight', { text: deviceText(roof.daylight.text) }),
        t('move.daylightWhy'),
        t('move.clears.daylight'),
        settings('daylight', 'move.fix.daylight'),
      )
    : null;

const parkBlocker = (roof: RoofStatus): Blocker | null =>
  roof.parkConfirmed
    ? null
    : make(
        'park',
        'warn',
        t('move.park', { text: deviceText(roof.parkText) || t('roof.reason.notParked') }),
        t('move.parkWhy'),
        t('move.clears.park'),
        settings('park', 'move.fix.park'),
      );

const supplyBlocker = (roof: RoofStatus): Blocker | null => {
  if (!roof.supplyLow) return null;
  const min = formatNumber(roof.minSupplyVolts, 1);
  const title =
    roof.supplyVolts === null
      ? t('roof.reason.supplyUnreadable', { min })
      : t('roof.reason.supplyLow', { volts: formatNumber(roof.supplyVolts, 1), min });
  return make('supply', 'warn', title, t('move.supplyWhy'), t('move.clears.supply', { min }), settings('current', 'move.fix.supply'));
};

const hallBlocker = (roof: RoofStatus): Blocker | null => {
  if (roof.hall.level === 'unsettled') return make('hall', 'warn', t('roof.reason.hallNotSettled'), t('move.hallSettleWhy'), t('move.clears.hallSettle'));
  if (roof.hall.level === 'rail')
    return make('hall', 'bad', t('move.hallRail', { counts: roof.hall.counts }), t('move.hallRailWhy'), t('move.clears.hallRail'), {
      label: t('move.fix.wiring'),
      href: '/pins',
    });
  return null;
};

const busyBlocker = (roof: RoofStatus, action: MoveAction): Blocker | null => {
  if (roof.calibrating) return make('calibrating', 'warn', t('move.calibrating'), t('move.calibratingWhy'), t('move.clears.calibrating'));
  if (roof.closePending) return make('closeWaiting', 'warn', t('roof.reason.closeWaiting'), t('move.closeWaitingWhy'), t('move.clears.closeWaiting'));
  if (action === 'move' && (roof.state === 'opening' || roof.state === 'closing'))
    return make('moving', 'warn', t('move.moving'), t('move.movingWhy'), t('move.clears.moving'));
  return null;
};

/** No live data: nothing can be offered, whatever the last reading said. */
export const noLiveBlocker = (): Blocker => make('noLiveData', 'warn', t('roof.reason.noLiveData'), t('move.noLiveWhy'), t('move.clears.live'));

/**
 * Everything that stops `action` now, most important first, in the device's order of checks (the
 * calibration run's and jog's start checks, spec 016 FR-001 to FR-004; Open and Close's gates, spec 003).
 * Empty when nothing does. "Already open" and the like aren't blockers: the button says those itself.
 */
export const moveBlockers = (roof: RoofStatus | null | undefined, action: MoveAction, ctx: BlockerContext): Blocker[] => {
  if (!ctx.live || !roof) return [noLiveBlocker()];
  const found: (Blocker | null)[] = [faultBlocker(roof)];
  // A fault already says why the automatic close can't move the roof.
  if (action === 'move' && !roof.fault) found.push(closePhaseBlocker(roof, ctx));
  found.push(busyBlocker(roof, action));
  if (action !== 'close') found.push(weatherBlocker(roof), daylightBlocker(roof));
  if (action !== 'open') found.push(parkBlocker(roof));
  found.push(supplyBlocker(roof), hallBlocker(roof));
  return found.filter((item): item is Blocker => item !== null);
};

/** The checks a test box bypass stands in for now, by name (never shown as passing, constitution VII). */
export const bypassedChecks = (roof: RoofStatus | null | undefined): string[] => {
  const box = roof?.testBox;
  if (!box?.active) return [];
  return (['park', 'weather', 'daylight'] as const).filter((check) => box[check]).map((check) => t(`testBox.check.${check}`));
};
