import { ComponentChildren, FunctionalComponent } from 'preact';
import { useId } from 'preact/hooks';
import type { RoofConfig, RoofStatus, RoofSwitch } from '../types';
import { Button, Card, InfoTip, Note, Pill, ReadingRow } from '../components/ui';
import {
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

// The reason under the buttons; one note when Open and Close share it.
const reasonNotes = (open: ButtonView, close: ButtonView, ids: { open: string; close: string }) => {
  const notes: ComponentChildren[] = [];
  if (open.reason)
    notes.push(
      <Note key="open" id={ids.open}>
        {t('roof.reason.openPrefix', { reason: open.reason })}
      </Note>,
    );
  if (close.reason && close.reason !== open.reason)
    notes.push(
      <Note key="close" id={ids.close}>
        {t('roof.reason.closePrefix', { reason: close.reason })}
      </Note>,
    );
  return notes;
};

const Buttons: FunctionalComponent<{ view: RoofView; send: Send }> = ({ view, send }) => {
  const base = useId();
  const ids = { open: `${base}-open`, close: `${base}-close` };
  const closeId = view.close.reason && view.close.reason === view.open.reason ? ids.open : ids.close;
  return (
    <div class="roof-controls" data-inventory="roof-buttons">
      <div class="btn-row">
        <Button
          onClick={() => send('open')}
          disabled={view.open.disabled}
          busy={view.open.busy}
          busyLabel={t('roof.sending')}
          describedBy={view.open.reason ? ids.open : undefined}
        >
          {t('roof.open')}
        </Button>
        <Button
          onClick={() => send('close')}
          disabled={view.close.disabled}
          busy={view.close.busy}
          busyLabel={t('roof.sending')}
          describedBy={view.close.reason ? closeId : undefined}
        >
          {t('roof.close')}
        </Button>
        {/* Never disabled, never busy (constitution VII, FR-003). */}
        <Button variant="danger" onClick={() => send('stop')}>
          {t('roof.stop')}
        </Button>
      </div>
      {reasonNotes(view.open, view.close, ids)}
      {view.notes.map((note) => (
        <Note key={note.id} tone={note.tone}>
          {note.text}
        </Note>
      ))}
      {view.result && <Note tone={view.result.tone}>{view.result.text}</Note>}
    </div>
  );
};

const Fault: FunctionalComponent<{ roof: RoofStatus; send: Send; busy: boolean }> = ({ roof, send, busy }) =>
  roof.fault ? (
    <div class="roof-fault" data-inventory="roof-fault-card">
      <Note tone="bad">{deviceText(roof.fault.text)}</Note>
      <Button small onClick={() => send('clear-fault')} busy={busy} busyLabel={t('roof.sending')}>
        {t('roof.clearFault')}
      </Button>
    </div>
  ) : null;

const SWITCH_KEY = { closed: 'roof.switch.closed', open: 'roof.switch.open', unreadable: 'roof.switch.unreadable' } as const;
const switchText = (value: RoofSwitch) => (value === 'notFitted' ? '' : t(SWITCH_KEY[value]));

const Checks: FunctionalComponent<{ roof: RoofStatus }> = ({ roof }) => {
  const phase = closePhasePill(roof.closePhase);
  return (
    <div data-inventory="roof-checks">
      <PillRow label={t('roof.weather.label')} pill={weatherPill(roof)} />
      <PillRow label={t('roof.park.label')} pill={parkPill(roof)} />
      {phase && <PillRow label={t('roof.closePhase.label')} pill={phase} id="roof-close-phase" />}
    </div>
  );
};

const supplyText = (roof: RoofStatus) =>
  roof.supplyVolts === null ? t('roof.unreadable') : t('roof.volts', { value: formatNumber(roof.supplyVolts, 1) });
const ampsText = (amps: number | null) => (amps === null ? t('roof.unreadable') : t('roof.amps', { value: formatNumber(amps, 1) }));

const Readings: FunctionalComponent<{ roof: RoofStatus }> = ({ roof }) => (
  <div data-inventory="roof-readings">
    <ReadingRow label={t('roof.supply')} value={supplyText(roof)} valueClass={roof.supplyLow ? 'tone-red' : ''} />
    {roof.currentSense && <ReadingRow label={t('roof.motorCurrent')} value={ampsText(roof.motorAmps)} />}
    {roof.parkSwitches.fitted >= 1 && <ReadingRow label={t('roof.parkSwitch1')} value={switchText(roof.parkSwitches.switch1)} />}
    {roof.parkSwitches.fitted >= 2 && <ReadingRow label={t('roof.parkSwitch2')} value={switchText(roof.parkSwitches.switch2)} />}
    {roof.rainContact !== 'notFitted' && <ReadingRow label={t('roof.rainContact')} value={t(`roof.rain.${roof.rainContact}`)} />}
  </div>
);

const Hall: FunctionalComponent<{ roof: RoofStatus; config?: RoofConfig }> = ({ roof, config }) => (
  <div class="roof-hall" data-inventory="roof-hall">
    <div class="reading-row">
      <span class="reading-label">
        {t('roof.hall.label')} <InfoTip text={t('roof.hall.hint')} />
      </span>
      <strong class="reading-value">{formatCount(roof.hall.counts)}</strong>
    </div>
    <HallScale counts={roof.hall.counts} low={config?.hallLowCounts} high={config?.hallHighCounts} />
    <Note>{t(`roof.hall.level.${roof.hall.level}`)}</Note>
  </div>
);

export interface RoofCardProps {
  live: LiveState;
  config?: RoofConfig;
}

const RoofCard: FunctionalComponent<RoofCardProps> = ({ live, config }) => {
  const roof = live.status?.roof ?? null;
  const { result, inFlight, send } = useRoofCommand(roof?.state ?? null);
  const view = roofView({ live, result, inFlight, now: Date.now() });
  return (
    <div class="roof-card" data-inventory="roof-card">
      <Card title={t('roof.title')} icon="roof" hint={t('roof.hint')} actions={<Pill tone={view.state.tone}>{view.state.label}</Pill>}>
        <Position view={view} />
        <Buttons view={view} send={send} />
        {roof && <Fault roof={roof} send={send} busy={inFlight === 'clear-fault'} />}
        {roof && <Checks roof={roof} />}
        {roof && <Readings roof={roof} />}
        {roof && <Hall roof={roof} config={config} />}
      </Card>
    </div>
  );
};

export default RoofCard;
