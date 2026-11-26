import { ComponentChildren, FunctionalComponent } from 'preact';
import { useId } from 'preact/hooks';
import { route } from 'preact-router';
import type { AutoCloseConfig, LocationStatus, RoofConfig, RoofDaylight, RoofStatus, RoofSwitch } from '../types';
import { locationLine } from '../lib/locationView';
import { Button, Card, InfoTip, Note, Pill, ReadingRow } from '../components/ui';
import {
  clearFaultReason,
  closePhasePill,
  parkPill,
  roofView,
  weatherPill,
  type LiveState,
  type PillView,
  type RoofView,
} from '../lib/roofView';
import { useRoofCommand } from '../hooks/useRoofCommand';
import { closePhaseBlocker, faultBlocker, type BlockerContext } from '../lib/moveBlockers';
import { ninaLine, sqmeterLine, type HealthLine } from '../lib/sourceHealth';
import { MoveBlockers } from '../components/MoveBlockers';
import { RoofButtons, type Send } from './RoofButtons';
import { deviceText } from '../i18n/deviceMessage';
import { formatCount, formatNumber } from '../i18n/format';
import { t } from '../i18n';
import HallScale from './HallScale';

// The Roof card (spec 009 US1, US3; contracts/ui.md): the state, the position,
// Open, Close and Stop, the fault, the checks the roof's commands pass, the
// readings and the Hall scale. Stop is never disabled (constitution VII); a
// disabled Open or Close is described by the note with its reason.

// A row whose value is a pill, with its reason behind "?" (DS-02, DS-04).
// Spec 027: a source's health in good order goes in the "?" too, on its own line.
const PillRow: FunctionalComponent<{ label: string; pill: PillView; id?: string; extra?: string }> = ({ label, pill, id, extra }) => (
  <div class="reading-row" data-inventory={id}>
    <span class="reading-label">
      {label}{' '}
      {(pill.text || extra) && (
        <InfoTip
          text={
            <>
              {pill.text}
              {pill.text && extra && <br />}
              {extra}
            </>
          }
        />
      )}
    </span>
    <Pill tone={pill.tone}>{pill.label}</Pill>
  </div>
);

// Spec 027 (FR-015): SQMeter's last reply and N.I.N.A.'s reach; a problem is a short note under its row.
// Spec 034: the device's own reason goes in the row's "?", once (the pill's text often carries it already).
const HealthRow: FunctionalComponent<{ label: string; pill: PillView; line: HealthLine | null }> = ({ label, pill, line }) => {
  const detail = line?.detail && !pill.text.includes(line.detail) ? line.detail : undefined;
  return (
    <>
      <PillRow label={label} pill={pill} extra={line?.ok ? line.text : detail} />
      {line && !line.ok && <Note tone="warn">{line.text}</Note>}
    </>
  );
};

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

// Clear fault needs live data, so the person sees the fault they clear (constitution I).
// Spec 027: a Hall rail fault says how it clears and shows the live reading beside it.
const Fault: FunctionalComponent<{ roof: RoofStatus; send: Send; busy: boolean; reason: string }> = ({ roof, send, busy, reason }) => {
  const id = useId();
  if (!roof.fault) return null;
  const blocker = faultBlocker(roof);
  const rail = roof.fault.kind === 'hallRail';
  return (
    <div class="roof-fault" data-inventory="roof-fault-card">
      <Note tone="bad">
        {deviceText(roof.fault.text)} <InfoTip text={blocker?.meaning} />
      </Note>
      {rail && <ReadingRow label={t('roof.hall.label')} value={formatCount(roof.hall.counts)} />}
      {rail && <Note>{blocker?.clears}</Note>}
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
const HintRow: FunctionalComponent<{ label: string; hint: ComponentChildren; value: string; valueClass?: string }> = ({
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
// Spec 034: a failed read says only which location is used; why is in the row's "?".
export const DaylightRow: FunctionalComponent<{ daylight: RoofDaylight; location?: LocationStatus }> = ({ daylight, location }) => {
  const line = locationLine(location);
  const mode = t('roof.daylight.hint', { mode: t(`roof.daylight.mode.${daylight.mode}`) });
  return (
    <div data-inventory="roof-daylight">
      <HintRow
        label={t('roof.daylight.label')}
        hint={
          line?.why ? (
            <>
              {mode}
              <br />
              {line.why}
            </>
          ) : (
            mode
          )
        }
        value={deviceText(daylight.text)}
      />
      {line && <Note>{line.text}</Note>}
    </div>
  );
};

// Spec 030: the weather hold counting down to the automatic close (rain and a failed rain sensor need none).
const WeatherHold: FunctionalComponent<{ roof: RoofStatus }> = ({ roof }) => {
  const hold = roof.weatherHold;
  if (!hold || hold.fired || hold.leftSeconds < 0) return null;
  return (
    <div data-inventory="roof-weather-hold">
      <Note tone="warn">{t('roof.weatherHold', { seconds: formatCount(hold.leftSeconds) })}</Note>
    </div>
  );
};

const Checks: FunctionalComponent<{ roof: RoofStatus; location?: LocationStatus; ctx: BlockerContext }> = ({ roof, location, ctx }) => {
  const phase = closePhasePill(roof.closePhase);
  // Spec 026: a stand-down says when it clears, with Release hold while that is allowed; spec 034: the
  // pill above and its "?" already say what it is, so only that line.
  const hold = roof.closePhase === 'heldByPerson' || roof.closePhase === 'halted' ? closePhaseBlocker(roof, ctx) : null;
  return (
    <div data-inventory="roof-checks">
      <HealthRow label={t('roof.weather.label')} pill={weatherPill(roof)} line={sqmeterLine(roof)} />
      <WeatherHold roof={roof} />
      <HealthRow label={t('roof.park.label')} pill={parkPill(roof)} line={ninaLine(roof)} />
      {roof.daylight && <DaylightRow daylight={roof.daylight} location={location} />}
      {phase && <PillRow label={t('roof.closePhase.label')} pill={phase} id="roof-close-phase" />}
      {hold && <MoveBlockers blockers={[hold]} brief />}
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
  autoClose?: Pick<AutoCloseConfig, 'personStopLapse' | 'stopHoldMinutes'>; // spec 044: when a person's Stop lapses
}

// What the held line needs to say when the hold clears (spec 026; spec 044: a person's Stop lapsing).
const blockerContext = (live: boolean, rearmMinutes: number, nowMs: number, autoClose: RoofCardProps['autoClose']): BlockerContext => ({
  live,
  rearmMinutes,
  nowMs,
  stopLapse: autoClose?.personStopLapse,
  stopHoldMinutes: autoClose?.stopHoldMinutes,
});

const RoofCard: FunctionalComponent<RoofCardProps> = ({ live, config, confirm = false, rearmMinutes = 10, autoClose }) => {
  const roof = live.status?.roof ?? null;
  const { result, inFlight, send } = useRoofCommand(roof?.state ?? null);
  const input = { live, result, inFlight, now: Date.now() };
  const view = roofView(input);
  const clearReason = inFlight === 'open' || inFlight === 'close' ? t('roof.sending') : clearFaultReason(input);
  const ctx = blockerContext(view.live, rearmMinutes, input.now, autoClose);
  return (
    <div class="roof-card" data-inventory="roof-card">
      <Card title={t('roof.title')} icon="roof" hint={t('roof.hint')} actions={<Pill tone={view.state.tone}>{view.state.label}</Pill>}>
        <Position view={view} />
        {roof?.blind && (
          <div data-inventory="roof-blind">
            <Note tone="warn">{t('roof.blindClosing')}</Note>
          </div>
        )}
        <RoofButtons view={view} send={send} confirm={confirm} ctx={ctx} />
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
