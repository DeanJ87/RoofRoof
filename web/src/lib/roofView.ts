import type { RoofClosePhase, RoofState, RoofStatus, SystemStatus } from '../types';
import type { RoofCommand } from './api';
import { t } from '../i18n';
import { deviceText } from '../i18n/deviceMessage';
import { LIVE_QUIET_MS } from './liveStream';

// What the Roof card, the Stop bar and the Status card show for the roof
// (spec 009, data-model.md). The page only predicts refusals so that it can
// grey a button with its reason (constitution VII); the device decides every
// command (constitution I-III). Stop is never disabled.

export type CommandOutcome = 'accepted' | 'pending' | 'refused' | 'unreachable' | 'error';

/** The answer to the last command the page sent. */
export interface CommandResult {
  command: RoofCommand;
  outcome: CommandOutcome;
  reason: string; // the device's machine reason on a refusal ("busy", "restarting", ...)
  text: string; // the device's text, translated when known
  at: number; // ms, when the answer arrived
  stateAt: RoofState | null; // the roof's state then
}

/** How long a command's answer stays on the card unless the roof's state changes first. */
export const RESULT_MS = 15000;
/** A `restarting` refusal holds Open and Close until live data is back, or this long. */
export const RESTART_HOLD_MS = 30000;

export interface LiveState {
  status: SystemStatus | null;
  connected: boolean; // the socket is open
  quiet: boolean; // no message for more than 5 s
  reconnectedAt: number | null; // when a message last arrived after the socket was lost
  lastMessageAt?: number | null; // when the last message arrived (ms), checked against `now` too
}

/** No message for longer than this and the data counts as stale (useWebSocket's quiet limit). */
export const LIVE_MS = LIVE_QUIET_MS;

export interface ButtonView {
  disabled: boolean;
  reason: string; // why it is disabled; empty otherwise (a blocked button's reasons come from lib/moveBlockers.ts)
  busy: boolean; // a command is in flight
  blocked: boolean; // spec 027: usable, but a check stands in the way: pressing it says why (the move dialog)
}

export type PillTone = 'pill-green' | 'pill-amber' | 'pill-red' | 'pill-dim' | 'pill-cyan';
export interface PillView {
  label: string;
  tone: PillTone;
  text: string; // the reason, behind "?"
}
export type NoteTone = 'muted' | 'warn' | 'bad' | 'ok';
export interface NoteView {
  id: string;
  tone: NoteTone;
  text: string;
}

export interface RoofView {
  live: boolean;
  roof: RoofStatus | null;
  open: ButtonView;
  close: ButtonView;
  stop: { disabled: false };
  stopShown: boolean; // spec 027: only while it has something to stop
  state: PillView;
  position: { percent: number | null; text: string };
  notes: NoteView[];
  result: NoteView | null;
}

export interface ViewInput {
  live: LiveState;
  result: CommandResult | null;
  inFlight: RoofCommand | null;
  now: number;
}

// `now` catches a page that slept with the socket open: useWebSocket's check may lag.
export const isLive = (live: LiveState, now = Date.now()) =>
  live.status !== null && live.connected && !live.quiet && !(typeof live.lastMessageAt === 'number' && now - live.lastMessageAt > LIVE_MS);

const moving = (roof: RoofStatus | null) => roof?.state === 'opening' || roof?.state === 'closing';

/** A Stop that wasn't confirmed stays on screen until the roof is seen not moving (never on time alone). */
const unconfirmedStop = (result: CommandResult, roof: RoofStatus | null) =>
  result.command === 'stop' && result.outcome !== 'accepted' && (roof === null || moving(roof));

/** The last answer is still worth showing: young, and the roof hasn't changed state since it arrived. */
export const resultShown = (result: CommandResult | null, roof: RoofStatus | null, now: number): result is CommandResult =>
  result !== null &&
  (unconfirmedStop(result, roof) || (now - result.at < RESULT_MS && (result.stateAt === null || roof?.state === result.stateAt)));

/** A `restarting` refusal holds Open and Close until a message arrives after a reconnect, or 30 s pass. */
export const restartHeld = (result: CommandResult | null, live: LiveState, now: number) =>
  result?.reason === 'restarting' && now - result.at < RESTART_HOLD_MS && !(live.reconnectedAt !== null && live.reconnectedAt > result.at);

// Reasons both buttons share, first match wins.
const sharedReason = (input: ViewInput): string | null => {
  const { live, result, now } = input;
  if (!isLive(live, now)) return t('roof.reason.noLiveData');
  if (!live.status?.roof) return t('roof.reason.notReporting');
  if (restartHeld(result, live, now)) return t('roof.reason.restarting');
  if (live.status.roof.fault) return t('roof.state.fault');
  return null;
};

// Spec 034: what only blocks a button (a press opens the move dialog); the Roof card words it from
// lib/moveBlockers.ts. The motion logic refuses either direction on a low supply or an unsettled Hall (spec 002).
const motionBlocked = (roof: RoofStatus) => roof.supplyLow || roof.hall.level === 'unsettled';
const closeBlock = (roof: RoofStatus) => !roof.parkConfirmed || motionBlocked(roof);

const hotspotHeld = (input: ViewInput) =>
  Boolean(input.live.status?.wifi?.apMode) ||
  (resultShown(input.result, input.live.status?.roof ?? null, input.now) && input.result.reason === 'hotspot');

// Spec 027: what disables Open (nothing to do, or nothing a person can weigh), then what only blocks it.
const openReason = (input: ViewInput, roof: RoofStatus): string | null => {
  if (roof.state === 'open') return t('roof.reason.alreadyOpen');
  if (roof.state === 'opening') return t('roof.reason.opening');
  // Spec 024 (parkSwitchCheck): with switches only, a switch that hasn't opened in 3 roof openings.
  if (roof.parkSwitches.check) return deviceText(roof.parkSwitches.check);
  if (hotspotHeld(input)) return t('roof.reason.hotspot');
  return null;
};

const openBlock = (roof: RoofStatus) => roof.weather !== 'safe' || Boolean(roof.daylight?.closes) || closeBlock(roof);

const closeReason = (roof: RoofStatus): string | null => {
  if (roof.state === 'closed') return t('roof.reason.alreadyClosed');
  if (roof.state === 'closing') return t('roof.reason.closing');
  if (roof.closePending) return t('roof.reason.closeWaiting');
  return null;
};

const button = (input: ViewInput, own: (roof: RoofStatus) => string | null, block: (roof: RoofStatus) => boolean): ButtonView => {
  const shared = sharedReason(input);
  if (shared) return { disabled: true, reason: shared, busy: false, blocked: false };
  if (input.inFlight === 'open' || input.inFlight === 'close') return { disabled: true, reason: '', busy: true, blocked: false };
  if (input.inFlight === 'clear-fault') return { disabled: true, reason: t('roof.reason.clearingFault'), busy: false, blocked: false };
  const roof = input.live.status!.roof!;
  const reason = own(roof);
  if (reason !== null) return { disabled: true, reason, busy: false, blocked: false };
  return { disabled: false, reason: '', busy: false, blocked: block(roof) };
};

export const openButton = (input: ViewInput) => button(input, (roof) => openReason(input, roof), openBlock);
export const closeButton = (input: ViewInput) => button(input, closeReason, closeBlock);

const STATE_PILL: Record<RoofState, PillTone> = {
  closed: 'pill-green',
  open: 'pill-cyan',
  opening: 'pill-amber',
  closing: 'pill-amber',
  stopped: 'pill-amber',
  unknown: 'pill-dim',
  error: 'pill-red',
};

export const statePill = (roof: RoofStatus | null): PillView => {
  if (!roof) return { label: t('roof.state.notReporting'), tone: 'pill-dim', text: '' };
  if (roof.fault) return { label: t('roof.state.fault'), tone: 'pill-red', text: deviceText(roof.fault.text) };
  return { label: t(`roof.state.${roof.state}`), tone: STATE_PILL[roof.state], text: '' };
};

export const positionOf = (roof: RoofStatus | null): { percent: number | null; text: string } => {
  if (!roof || !roof.positionKnown || roof.positionPercent === null) return { percent: null, text: t('roof.position.unknown') };
  const percent = Math.max(0, Math.min(100, Math.round(roof.positionPercent)));
  return { percent, text: t('roof.position.percentOpen', { percent }) };
};

// Spec 022: a check the test box bypasses never says "Safe" or "Parked".
const BYPASSED: PillView = { label: '', tone: 'pill-red', text: '' };
const bypassed = (text: string): PillView => ({ ...BYPASSED, label: t('roof.testBox.bypassed'), text: deviceText(text) });

export const weatherPill = (roof: RoofStatus): PillView => {
  const text = deviceText(roof.weatherText);
  if (roof.testBox?.weather && roof.weather === 'safe') return bypassed(roof.weatherText);
  if (roof.weather === 'safe') return { label: t('roof.weather.safe'), tone: 'pill-green', text };
  if (roof.weather === 'unsafe') return { label: t('roof.weather.unsafe'), tone: 'pill-red', text };
  return { label: t('roof.weather.unknown'), tone: 'pill-amber', text };
};

export const parkPill = (roof: RoofStatus): PillView => {
  if (roof.testBox?.park && roof.parkConfirmed) return bypassed(roof.parkText);
  return roof.parkConfirmed
    ? { label: t('roof.park.parked'), tone: 'pill-green', text: deviceText(roof.parkText) }
    : { label: t('roof.park.notParked'), tone: 'pill-amber', text: deviceText(roof.parkText) };
};

// The automatic close (spec 005): attempting, held by a person's Stop, or stuck.
const ATTEMPTING: RoofClosePhase[] = ['waitPark', 'closing', 'retryWait'];
export const closeAttempting = (phase: RoofClosePhase) => ATTEMPTING.includes(phase);

export const closePhasePill = (phase: RoofClosePhase): PillView | null => {
  if (phase === 'idle') return null;
  const tone: PillTone = phase === 'cannotMove' || phase === 'halted' ? 'pill-red' : 'pill-amber';
  return { label: t(`roof.closePhase.${phase}`), tone, text: t(`roof.closePhase.${phase}Hint`) };
};

/** Seconds as m:ss, for a countdown. */
export const minSec = (seconds: number) => {
  const whole = Math.max(0, Math.round(seconds));
  return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, '0')}`;
};

const OVERRIDE_LABEL = {
  weatherNotSafe: 'roof.weather.label',
  daylight: 'roof.daylight.label',
  mountNotParked: 'roof.park.label',
} as const;

// Spec 027: a move waiting for the park, the last wait's failure, and a person's overrides in effect.
const parkNotes = (roof: RoofStatus): NoteView[] => {
  const notes: NoteView[] = [];
  const wait = roof.parkWait;
  if (wait) {
    const params = { command: t(wait.command === 'open' ? 'roof.open' : 'roof.close'), time: minSec(wait.leftSeconds) };
    notes.push({ id: 'park-wait', tone: 'warn', text: t(wait.asked ? 'roof.parkWait.asked' : 'roof.parkWait.waiting', params) });
  }
  if (roof.parkWaitEnded) notes.push({ id: 'park-wait-ended', tone: 'warn', text: deviceText(roof.parkWaitEnded) });
  const names = (roof.override ?? []).map((name) => t(OVERRIDE_LABEL[name]));
  if (names.length) notes.push({ id: 'override', tone: 'warn', text: t('roof.override.active', { checks: names.join(', ') }) });
  return notes;
};

const stateNotes = (roof: RoofStatus): NoteView[] => {
  const notes = parkNotes(roof);
  if (!roof.positionKnown) notes.push({ id: 'unknown-position', tone: 'muted', text: t('roof.note.positionUnknown') });
  if (closeAttempting(roof.closePhase)) notes.push({ id: 'auto-close', tone: 'warn', text: t('roof.note.stopCallsOffClose') });
  return notes;
};

const RESULT_TONE: Record<CommandOutcome, NoteTone> = {
  accepted: 'ok',
  pending: 'muted',
  refused: 'bad',
  unreachable: 'bad',
  error: 'bad',
};

/** The last command's answer as a note, or null when there's nothing to say. */
export const resultNote = (result: CommandResult | null, roof: RoofStatus | null, now: number): NoteView | null => {
  if (!resultShown(result, roof, now) || result.outcome === 'accepted') return null;
  return { id: 'result', tone: RESULT_TONE[result.outcome], text: result.text };
};

export const roofView = (input: ViewInput): RoofView => {
  const roof = input.live.status?.roof ?? null;
  return {
    live: isLive(input.live, input.now),
    roof,
    open: openButton(input),
    close: closeButton(input),
    stop: { disabled: false },
    // Spec 028: before the first status no state is shown, not even "not reporting".
    state: input.live.status ? statePill(roof) : { label: t('live.connecting'), tone: 'pill-dim', text: '' },
    stopShown: stopShown(roof, isLive(input.live, input.now)),
    position: positionOf(roof),
    notes: roof ? stateNotes(roof) : [],
    result: resultNote(input.result, roof, input.now),
  };
};

/**
 * Stop is pinned to every page while the roof was last seen moving or a close waits (FR-014),
 * and while no status has arrived at all for `waitedMs` (the roof's state is unknown).
 */
export const STOP_BAR_NO_STATUS_MS = 5000;
export const stopBarShown = (status: SystemStatus | null, waitedMs = 0) => {
  if (!status) return waitedMs >= STOP_BAR_NO_STATUS_MS;
  const roof = status.roof;
  // Spec 016: the whole run; spec 027: a move waiting for the park.
  return Boolean(roof && (moving(roof) || roof.closePending || roof.calibrating || roof.parkWait));
};

/**
 * Spec 027: the card's Stop shows while the roof moves, a close or a move waits, or a run is under way,
 * and whenever the page can't see the roof live (it might be moving). Never disabled.
 */
export const stopShown = (roof: RoofStatus | null, live: boolean) =>
  !live || !roof || moving(roof) || roof.closePending || Boolean(roof.calibrating) || Boolean(roof.parkWait);

/**
 * Clear fault needs live data: the person must see the fault they clear. A pin setup fault
 * (spec 023) can't be cleared: the device refuses until the pins are fixed and it restarts.
 */
export const clearFaultReason = (input: ViewInput): string => {
  if (!isLive(input.live, input.now)) return t('roof.reason.noLiveData');
  return input.live.status?.roof?.fault?.cause === 'pinSetup' ? t('roof.reason.pinSetup') : '';
};

export const HALL_MAX_COUNTS = 4095;

export interface HallMarks {
  reading: number; // 0-1 along the scale
  low: number | null;
  high: number | null;
}

const fraction = (counts: number) => Math.max(0, Math.min(1, counts / HALL_MAX_COUNTS));
const usable = (value: number | undefined): value is number => typeof value === 'number' && Number.isFinite(value);

/** Where the reading and the thresholds sit on the 0-4095 scale. */
export const hallMarks = (counts: number, low?: number, high?: number): HallMarks => ({
  reading: fraction(counts),
  low: usable(low) ? fraction(low) : null,
  high: usable(high) ? fraction(high) : null,
});
