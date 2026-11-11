import type { RoofClosePhase, RoofState, RoofStatus, SystemStatus } from '../types';
import type { RoofCommand } from './api';
import { t } from '../i18n';
import { deviceText } from '../i18n/deviceMessage';
import { formatNumber } from '../i18n/format';

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

/** No message for longer than this and the data counts as stale (useQuiet's threshold). */
export const LIVE_MS = 5000;

export interface ButtonView {
  disabled: boolean;
  reason: string; // why it is disabled; empty when enabled or busy
  busy: boolean; // a command is in flight
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

// `now` catches a page that slept with the socket open: useQuiet's tick may lag.
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

const volts = (value: number) => formatNumber(value, 1);

export const supplyReason = (roof: RoofStatus): string | null => {
  if (!roof.supplyLow) return null;
  return roof.supplyVolts === null
    ? t('roof.reason.supplyUnreadable', { min: volts(roof.minSupplyVolts) })
    : t('roof.reason.supplyLow', { volts: volts(roof.supplyVolts), min: volts(roof.minSupplyVolts) });
};

// Reasons both buttons share, first match wins.
const sharedReason = (input: ViewInput): string | null => {
  const { live, result, now } = input;
  if (!isLive(live, now)) return t('roof.reason.noLiveData');
  if (!live.status?.roof) return t('roof.reason.notReporting');
  if (restartHeld(result, live, now)) return t('roof.reason.restarting');
  if (live.status.roof.fault) return t('roof.reason.fault');
  return null;
};

// What the motion logic refuses for either direction (spec 002).
const motionReason = (roof: RoofStatus): string | null =>
  supplyReason(roof) ?? (roof.hall.level === 'unsettled' ? t('roof.reason.hallNotSettled') : null);

const hotspotHeld = (input: ViewInput) =>
  Boolean(input.live.status?.wifi?.apMode) ||
  (resultShown(input.result, input.live.status?.roof ?? null, input.now) && input.result.reason === 'hotspot');

const openReason = (input: ViewInput, roof: RoofStatus): string | null => {
  if (roof.state === 'open') return t('roof.reason.alreadyOpen');
  if (roof.state === 'opening') return t('roof.reason.opening');
  if (roof.weather !== 'safe') return t('roof.reason.weather', { text: deviceText(roof.weatherText) || t('roof.weather.unknown') });
  if (hotspotHeld(input)) return t('roof.reason.hotspot');
  return motionReason(roof);
};

const closeReason = (roof: RoofStatus): string | null => {
  if (roof.state === 'closed') return t('roof.reason.alreadyClosed');
  if (roof.state === 'closing') return t('roof.reason.closing');
  if (roof.closePending) return t('roof.reason.closeWaiting');
  if (!roof.parkConfirmed) return deviceText(roof.parkText) || t('roof.reason.notParked');
  return motionReason(roof);
};

const button = (input: ViewInput, own: (roof: RoofStatus) => string | null): ButtonView => {
  const shared = sharedReason(input);
  if (shared) return { disabled: true, reason: shared, busy: false };
  if (input.inFlight === 'open' || input.inFlight === 'close') return { disabled: true, reason: '', busy: true };
  if (input.inFlight === 'clear-fault') return { disabled: true, reason: t('roof.reason.clearingFault'), busy: false };
  const reason = own(input.live.status!.roof!);
  return { disabled: reason !== null, reason: reason ?? '', busy: false };
};

export const openButton = (input: ViewInput) => button(input, (roof) => openReason(input, roof));
export const closeButton = (input: ViewInput) => button(input, closeReason);

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

export const weatherPill = (roof: RoofStatus): PillView => {
  const text = deviceText(roof.weatherText);
  if (roof.weather === 'safe') return { label: t('roof.weather.safe'), tone: 'pill-green', text };
  if (roof.weather === 'unsafe') return { label: t('roof.weather.unsafe'), tone: 'pill-red', text };
  return { label: t('roof.weather.unknown'), tone: 'pill-amber', text };
};

export const parkPill = (roof: RoofStatus): PillView =>
  roof.parkConfirmed
    ? { label: t('roof.park.parked'), tone: 'pill-green', text: deviceText(roof.parkText) }
    : { label: t('roof.park.notParked'), tone: 'pill-amber', text: deviceText(roof.parkText) };

// The automatic close (spec 005): attempting, held by a person's Stop, or stuck.
const ATTEMPTING: RoofClosePhase[] = ['waitPark', 'closing', 'retryWait'];
export const closeAttempting = (phase: RoofClosePhase) => ATTEMPTING.includes(phase);

export const closePhasePill = (phase: RoofClosePhase): PillView | null => {
  if (phase === 'idle') return null;
  const tone: PillTone = phase === 'cannotMove' || phase === 'halted' ? 'pill-red' : 'pill-amber';
  return { label: t(`roof.closePhase.${phase}`), tone, text: t(`roof.closePhase.${phase}Hint`) };
};

const stateNotes = (roof: RoofStatus): NoteView[] => {
  const notes: NoteView[] = [];
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
    state: statePill(roof),
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
  return Boolean(roof && (moving(roof) || roof.closePending));
};

/** Clear fault needs live data: the person must see the fault they clear. */
export const clearFaultReason = (input: ViewInput): string => (isLive(input.live, input.now) ? '' : t('roof.reason.noLiveData'));

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
