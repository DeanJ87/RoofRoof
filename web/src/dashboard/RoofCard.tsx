import { ComponentChildren, FunctionalComponent } from 'preact';
import { useId, useState } from 'preact/hooks';
import { route } from 'preact-router';
import type { LocationStatus, RoofConfig, RoofDaylight, RoofStatus, RoofSwitch } from '../types';
import { locationLine } from '../lib/locationView';
import { Button, Card, InfoTip, Note, Pill, ReadingRow } from '../components/ui';
import {
  clearFaultReason,
  closePhasePill,
  parkPill,
  roofView,
  weatherPill,
  type ButtonView,
  type LiveState,
  type PillView,
  type RoofView,
} from '../lib/roofView';
import { useRoofCommand } from '../hooks/useRoofCommand';
import { closePhaseBlocker, faultBlocker, moveBlockers, type Blocker, type BlockerContext } from '../lib/moveBlockers';
import { MoveBlockers } from '../components/MoveBlockers';
import type { RoofCommand } from '../lib/api';
import { deviceText } from '../i18n/deviceMessage';
import { formatCount, formatNumber } from '../i18n/format';
import { t } from '../i18n';
import HallScale from './HallScale';

// The Roof card (spec 009 US1, US3; contracts/ui.md): the state, the position,
// Open, Close and Stop, the fault, the checks the roof's commands pass, the
// readings and the Hall scale. Stop is never disabled (constitution VII); a
// disabled Open or Close is described by the note with its reason.

// A row whose value is a pill, with its reason behind "?" (DS-02, DS-04).
const PillRow: FunctionalComponent<{ label: string; pill: PillView; id?: string }> = ({ label, pill, id }) => (
  <div class="reading-row" data-inventory={id}>
    <span class="reading-label">
      {label} {pill.text && <InfoTip text={pill.text} />}
    </span>
    <Pill tone={pill.tone}>{pill.label}</Pill>
  </div>
);

const Position: FunctionalComponent<{ view: RoofView }> = ({ view }) => (
  <div data-inventory="roof-position">
    <ReadingRow label={t('roof.position.label')} value={view.position.text} />
    <div
      class="progress-meter"
      role="meter"
      aria-label={t('roof.position.label')}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={view.position.percent ?? undefined}
      aria-valuetext={view.position.text}
    >
      <div style={{ width: `${view.position.percent ?? 0}%` }} />
    </div>
  </div>
);

type Send = (command: RoofCommand) => void;

// Spec 026: why Open or Close can't be used, from the shared blockers (the fault has its own
// block below). A reason no blocker covers ("Already open", "Restarting") stays a one-line note.
const blockersFor = (view: RoofView, ctx: BlockerContext): Blocker[] => {
  const roof = view.roof;
  const wanted: ('open' | 'close')[] = [];
  if (view.open.disabled && roof?.state !== 'open' && roof?.state !== 'opening') wanted.push('open');
  if (view.close.disabled && roof?.state !== 'closed' && roof?.state !== 'closing') wanted.push('close');
  const seen = new Set<string>(['fault']);
  return wanted
    .flatMap((action) => moveBlockers(roof, action, ctx))
    .filter((blocker) => !seen.has(blocker.id) && Boolean(seen.add(blocker.id)));
};

const reasonNotes = (open: ButtonView, close: ButtonView, ids: { open: string; close: string }, covered: boolean) => {
  const notes: ComponentChildren[] = [];
  if (open.reason && !covered)
    notes.push(
      <Note key="open" id={ids.open}>
        {t('roof.reason.openPrefix', { reason: open.reason })}
      </Note>,
    );
  if (close.reason && close.reason !== open.reason && !covered)
    notes.push(
      <Note key="close" id={ids.close}>
        {t('roof.reason.closePrefix', { reason: close.reason })}
      </Note>,
    );
  return notes;
};

// Spec 014: with "Ask before Open and Close" on, Open and Close ask once
// inline (the existing inline confirm pattern); Stop never asks (constitution VII).
const Confirm: FunctionalComponent<{ asking: 'open' | 'close'; onYes: () => void; onNo: () => void }> = ({ asking, onYes, onNo }) => {
  const question = asking === 'open' ? t('roof.confirmOpen') : t('roof.confirmClose');
  return (
    <div class="inline-confirm" role="group" aria-label={question} data-inventory="roof-confirm">
      <span>{question}</span>
      <Button onClick={onYes}>{asking === 'open' ? t('roof.open') : t('roof.close')}</Button>
      <Button variant="ghost" onClick={onNo}>
        {t('roof.cancel')}
      </Button>
    </div>
  );
};

const Buttons: FunctionalComponent<{ view: RoofView; send: Send; confirm: boolean; ctx: BlockerContext }> = ({
  view,
  send,
  confirm,
  ctx,
}) => {
  const base = useId();
  const [asking, setAsking] = useState<'open' | 'close' | null>(null);
  const blockers = blockersFor(view, ctx);
  const covered = blockers.length > 0;
  const ids = covered ? { open: `${base}-why`, close: `${base}-why` } : { open: `${base}-open`, close: `${base}-close` };
  const closeId = view.close.reason && view.close.reason === view.open.reason ? ids.open : ids.close;
  const press = (command: 'open' | 'close') => (confirm ? setAsking(command) : send(command));
  return (
    <div class="roof-controls" data-inventory="roof-buttons">
      <div class="btn-row">
        <Button
          onClick={() => press('open')}
          disabled={view.open.disabled}
          busy={view.open.busy}
          busyLabel={t('roof.sending')}
          describedBy={view.open.reason ? ids.open : undefined}
        >
          {t('roof.open')}
        </Button>
        <Button
          onClick={() => press('close')}
          disabled={view.close.disabled}
          busy={view.close.busy}
          busyLabel={t('roof.sending')}
          describedBy={view.close.reason ? closeId : undefined}
        >
          {t('roof.close')}
        </Button>
        {/* Never disabled, never busy (constitution VII, FR-003). */}
        <Button
          variant="danger"
          onClick={() => {
            setAsking(null);
            send('stop');
          }}
        >
          {t('roof.stop')}
        </Button>
      </div>
      {/* The question goes with the button: gone once the button is unavailable. */}
      {asking && !view[asking].disabled && (
        <Confirm
          asking={asking}
          onYes={() => {
            setAsking(null);
            send(asking);
          }}
          onNo={() => setAsking(null)}
        />
      )}
      <Why view={view} blockers={blockers} ids={ids} whyId={`${base}-why`} />
    </div>
  );
};

// Under the buttons: the shared blockers (spec 026), a reason no blocker covers, the notes and the last answer.
const Why: FunctionalComponent<{ view: RoofView; blockers: Blocker[]; ids: { open: string; close: string }; whyId: string }> = ({
  view,
  blockers,
  ids,
  whyId,
}) => (
  <>
    {blockers.length > 0 && (
      <div id={whyId}>
        <MoveBlockers blockers={blockers} />
      </div>
    )}
    {reasonNotes(view.open, view.close, ids, blockers.length > 0)}
    {view.notes.map((note) => (
      <Note key={note.id} tone={note.tone}>
        {note.text}
      </Note>
    ))}
    {view.result && <Note tone={view.result.tone}>{view.result.text}</Note>}
  </>
);

// Clear fault needs live data, so the person sees the fault they clear (constitution I).
const Fault: FunctionalComponent<{ roof: RoofStatus; send: Send; busy: boolean; reason: string }> = ({ roof, send, busy, reason }) => {
  const id = useId();
  if (!roof.fault) return null;
  return (
    <div class="roof-fault" data-inventory="roof-fault-card">
      <Note tone="bad">
        {deviceText(roof.fault.text)} <InfoTip text={faultBlocker(roof)?.meaning} />
      </Note>
      <Button
        small
        onClick={() => send('clear-fault')}
        disabled={reason !== ''}
        busy={busy}
        busyLabel={t('roof.sending')}
        describedBy={reason ? id : undefined}
      >
        {t('roof.clearFault')}
      </Button>
      {reason && <Note id={id}>{t('roof.reason.clearFaultPrefix', { reason })}</Note>}
    </div>
  );
};

const SWITCH_KEY = { closed: 'roof.switch.closed', open: 'roof.switch.open', unreadable: 'roof.switch.unreadable' } as const;
const switchText = (value: RoofSwitch) => (value === 'notFitted' ? '' : t(SWITCH_KEY[value]));

// A reading with its limit behind "?" (FR-011).
const HintRow: FunctionalComponent<{ label: string; hint: string; value: string; valueClass?: string }> = ({
  label,
  hint,
  value,
  valueClass = '',
}) => (
  <div class="reading-row">
    <span class="reading-label">
      {label} <InfoTip text={hint} />
    </span>
    <strong class={`reading-value ${valueClass}`}>{value}</strong>
  </div>
);

// The daylight check (spec 013): what the sun is doing as the device words it, the mode behind "?".
// Spec 020: under it, where the location came from and how old it is; never a verdict (FR-011).
export const DaylightRow: FunctionalComponent<{ daylight: RoofDaylight; location?: LocationStatus }> = ({ daylight, location }) => {
  const line = locationLine(location);
  return (
    <div data-inventory="roof-daylight">
      <HintRow
        label={t('roof.daylight.label')}
        hint={t('roof.daylight.hint', { mode: t(`roof.daylight.mode.${daylight.mode}`) })}
        value={deviceText(daylight.text)}
      />
      {line && <Note>{line.text}</Note>}
    </div>
  );
};

const Checks: FunctionalComponent<{ roof: RoofStatus; location?: LocationStatus; ctx: BlockerContext }> = ({ roof, location, ctx }) => {
  const phase = closePhasePill(roof.closePhase);
  // Spec 026: a stand-down says when it clears, with Release hold while that is allowed.
  const hold = roof.closePhase === 'heldByPerson' || roof.closePhase === 'halted' ? closePhaseBlocker(roof, ctx) : null;
  return (
    <div data-inventory="roof-checks">
      <PillRow label={t('roof.weather.label')} pill={weatherPill(roof)} />
      <PillRow label={t('roof.park.label')} pill={parkPill(roof)} />
      {roof.daylight && <DaylightRow daylight={roof.daylight} location={location} />}
      {phase && <PillRow label={t('roof.closePhase.label')} pill={phase} id="roof-close-phase" />}
      {hold && <MoveBlockers blockers={[hold]} />}
    </div>
  );
};

const supplyText = (roof: RoofStatus) =>
  roof.supplyVolts === null ? t('roof.unreadable') : t('roof.volts', { value: formatNumber(roof.supplyVolts, 1) });
const ampsText = (amps: number | null) => (amps === null ? t('roof.unreadable') : t('roof.amps', { value: formatNumber(amps, 1) }));

const Readings: FunctionalComponent<{ roof: RoofStatus }> = ({ roof }) => (
  <div data-inventory="roof-readings">
    <HintRow
      label={t('roof.supply')}
      hint={t('roof.supplyHint', { min: formatNumber(roof.minSupplyVolts, 1) })}
      value={supplyText(roof)}
      valueClass={roof.supplyLow ? 'tone-red' : ''}
    />
    {roof.currentSense && (
      <HintRow
        label={t('roof.motorCurrent')}
        hint={t('roof.motorCurrentHint', { max: formatNumber(roof.maxCurrentAmps, 1) })}
        value={ampsText(roof.motorAmps)}
      />
    )}
    {roof.parkSwitches.fitted >= 1 && <ReadingRow label={t('roof.parkSwitch1')} value={switchText(roof.parkSwitches.switch1)} />}
    {roof.parkSwitches.fitted >= 2 && <ReadingRow label={t('roof.parkSwitch2')} value={switchText(roof.parkSwitches.switch2)} />}
  </div>
);

const Hall: FunctionalComponent<{ roof: RoofStatus; config?: RoofConfig }> = ({ roof, config }) => (
  <div class="roof-hall" data-inventory="roof-hall">
    <HintRow label={t('roof.hall.label')} hint={t('roof.hall.hint')} value={formatCount(roof.hall.counts)} />
    <HallScale counts={roof.hall.counts} low={config?.hallLowCounts} high={config?.hallHighCounts} />
    <Note>{t(`roof.hall.level.${roof.hall.level}`)}</Note>
  </div>
);

// Spec 023: with an invalid pin setup the fault above says why; this is the way to the fix.
const PinsLink: FunctionalComponent<{ valid?: boolean }> = ({ valid }) =>
  valid === false ? (
    <div class="btn-row" data-inventory="roof-pins">
      <Button variant="link" onClick={() => route('/pins')}>
        {t('roof.pinsLink')}
      </Button>
    </div>
  ) : null;

// The fault, the checks, the readings and the Hall scale, once the roof reports.
const Details: FunctionalComponent<{
  roof: RoofStatus;
  config?: RoofConfig;
  live: LiveState;
  ctx: BlockerContext;
  fault: { send: Send; busy: boolean; reason: string };
}> = ({ roof, config, live, ctx, fault }) => (
  <>
    <Fault roof={roof} send={fault.send} busy={fault.busy} reason={fault.reason} />
    <Checks roof={roof} location={live.status?.location} ctx={ctx} />
    <Readings roof={roof} />
    <Hall roof={roof} config={config} />
  </>
);

export interface RoofCardProps {
  live: LiveState;
  config?: RoofConfig;
  confirm?: boolean; // spec 014: ui.confirmOpenClose
  rearmMinutes?: number; // autoClose.rearmMinutes, for "clears at" (spec 026)
}

const RoofCard: FunctionalComponent<RoofCardProps> = ({ live, config, confirm = false, rearmMinutes = 10 }) => {
  const roof = live.status?.roof ?? null;
  const { result, inFlight, send } = useRoofCommand(roof?.state ?? null);
  const input = { live, result, inFlight, now: Date.now() };
  const view = roofView(input);
  const clearReason = inFlight === 'open' || inFlight === 'close' ? t('roof.sending') : clearFaultReason(input);
  const ctx: BlockerContext = { live: view.live, rearmMinutes, nowMs: input.now };
  return (
    <div class="roof-card" data-inventory="roof-card">
      <Card title={t('roof.title')} icon="roof" hint={t('roof.hint')} actions={<Pill tone={view.state.tone}>{view.state.label}</Pill>}>
        <Position view={view} />
        <Buttons view={view} send={send} confirm={confirm} ctx={ctx} />
        <PinsLink valid={live.status?.pins?.valid} />
        {roof && (
          <Details
            roof={roof}
            config={config}
            live={live}
            ctx={ctx}
            fault={{ send, busy: inFlight === 'clear-fault', reason: clearReason }}
          />
        )}
      </Card>
    </div>
  );
};

export default RoofCard;
