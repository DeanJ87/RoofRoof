import { FunctionalComponent, type ComponentChildren } from 'preact';
import { useState } from 'preact/hooks';
import type { JogDirection, RoofConfig, RoofStatus, TeachMark, TeachPlace, TeachProposal, TeachStatus } from '../../types';
import { useJog } from '../../hooks/useJog';
import { confirmTeach, discardTeach, markTeach } from '../../lib/teachApi';
import { roofCommand } from '../../lib/api';
import { Note, Button, ReadingRow } from '../ui';
import { SettingsCard, Toggle } from '../settings/controls';
import HallScale from '../../dashboard/HallScale';
import { t } from '../../i18n';
import { formatNumber } from '../../i18n/format';
import { deviceText } from '../../i18n/deviceMessage';

// Jog and teach the ends (spec 019): a person at the roof holds a direction
// to move it at the slow duty, marks the closed end, the open end and a place
// part-way, and confirms the proposed Hall settings. The device checks every
// step again and stops a jog by itself when the keep-alives stop.

type Key = Parameters<typeof t>[0];

const PLACES: { at: TeachPlace; button: Key; label: Key }[] = [
  { at: 'closed', button: 'teach.markClosed', label: 'teach.closedReading' },
  { at: 'partWay', button: 'teach.markPartWay', label: 'teach.partWayReading' },
  { at: 'open', button: 'teach.markOpen', label: 'teach.openReading' },
];

const markText = (mark: TeachMark | null) =>
  mark ? t('teach.markValue', { mean: formatNumber(mark.mean, 0), noise: formatNumber(mark.noise, 1) }) : t('teach.notMarked');

const endText = (end: string) => (end === 'open' ? t('settings.roof.endOpen') : t('settings.roof.endClosed'));

const pair = (saved: string, proposed: string) => t(saved === proposed ? 'calibrate.same' : 'calibrate.change', { saved, proposed });

// A button that runs while it is held: pointer, touch or the keyboard (Space
// or Enter held). Leaving it, cancelling the pointer or losing focus releases it.
const HoldButton: FunctionalComponent<{
  direction: JogDirection;
  held: boolean;
  disabled: boolean;
  describedBy?: string;
  onPress: (direction: JogDirection) => void;
  onRelease: () => void;
}> = ({ direction, held, disabled, describedBy, onPress, onRelease, children }) => {
  const down = (event: Event) => {
    event.preventDefault();
    if (!disabled) onPress(direction);
  };
  const keyDown = (event: KeyboardEvent) => {
    if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) down(event);
  };
  const keyUp = (event: KeyboardEvent) => {
    if (event.key === ' ' || event.key === 'Enter') onRelease();
  };
  return (
    <button
      type="button"
      class={`btn btn-${held ? 'primary' : 'default'}`}
      aria-pressed={held}
      aria-describedby={describedBy}
      disabled={disabled && !held}
      data-jog={direction}
      onPointerDown={down}
      onPointerUp={onRelease}
      onPointerCancel={onRelease}
      onPointerLeave={onRelease}
      onBlur={onRelease}
      onKeyDown={keyDown}
      onKeyUp={keyUp}
      onContextMenu={(event) => event.preventDefault()}
    >
      {children}
    </button>
  );
};

interface Outcome {
  tone: 'ok' | 'bad';
  text: string;
}

const Proposal: FunctionalComponent<{ proposal: TeachProposal; config: RoofConfig; onDone: (outcome: Outcome) => void }> = ({
  proposal,
  config,
  onDone,
}) => {
  const [busy, setBusy] = useState(false);
  const discard = async () => {
    await discardTeach();
    onDone({ tone: 'ok', text: t('teach.discarded') });
  };
  if (!proposal.ok)
    return (
      <div data-teach-proposal>
        <Note tone="warn">{deviceText(proposal.text)}</Note>
        <div class="btn-row">
          <Button variant="ghost" onClick={() => void discard()}>
            {t('teach.startAgain')}
          </Button>
        </div>
      </div>
    );
  const confirm = async () => {
    setBusy(true);
    const { hallHighEnd, hallHighCounts, hallLowCounts } = proposal;
    const answer = await confirmTeach({ hallHighEnd, hallHighCounts, hallLowCounts });
    setBusy(false);
    onDone(answer.ok ? { tone: 'ok', text: t('teach.saved') } : { tone: 'bad', text: answer.text || t('teach.notSaved') });
  };
  return (
    <div data-teach-proposal>
      <h3 class="card-group-title">{t('teach.proposal')}</h3>
      <ReadingRow label={t('settings.roof.hallHighEnd')} value={pair(endText(config.hallHighEnd), endText(proposal.hallHighEnd))} />
      <ReadingRow label={t('settings.roof.hallHigh')} value={pair(String(config.hallHighCounts), String(proposal.hallHighCounts))} />
      <ReadingRow label={t('settings.roof.hallLow')} value={pair(String(config.hallLowCounts), String(proposal.hallLowCounts))} />
      <div class="btn-row">
        <Button variant="primary" onClick={() => void confirm()} busy={busy} busyLabel={t('common.saving')}>
          {t('teach.confirm')}
        </Button>
        <Button variant="ghost" onClick={() => void discard()}>
          {t('teach.discard')}
        </Button>
      </div>
    </div>
  );
};

// The two hold-to-run buttons, and Stop while the roof moves (never disabled).
const JogControls: FunctionalComponent<{
  why: string;
  moving: boolean;
  held: JogDirection | null;
  onPress: (direction: JogDirection) => void;
  onRelease: () => void;
}> = ({ why, moving, held, onPress, onRelease }) => (
  <>
    <div class="btn-row" data-teach-jog>
      {(['close', 'open'] as const).map((direction) => (
        <HoldButton
          key={direction}
          direction={direction}
          held={held === direction}
          disabled={why !== ''}
          describedBy="teach-why"
          onPress={onPress}
          onRelease={onRelease}
        >
          {t(direction === 'open' ? 'teach.holdOpen' : 'teach.holdClose')}
        </HoldButton>
      ))}
    </div>
    {/* In its own row, so its showing never moves a held button from under the pointer. */}
    {(moving || held) && (
      <div class="btn-row">
        <Button variant="danger" onClick={() => void roofCommand('stop').catch(() => undefined)}>
          {t('teach.stop')}
        </Button>
      </div>
    )}
    <Note id="teach-why">{why || t('teach.holdHint')}</Note>
  </>
);

// The three places with their readings, and the buttons that mark them.
const Marks: FunctionalComponent<{ teach: TeachStatus | null; ready: boolean; onMark: (at: TeachPlace) => void }> = ({
  teach,
  ready,
  onMark,
}) => (
  <>
    <h3 class="card-group-title">{t('teach.marks')}</h3>
    {PLACES.map((place) => (
      <ReadingRow key={place.at} label={t(place.label)} value={markText(teach?.marks[place.at] ?? null)} />
    ))}
    <div class="btn-row" data-teach-marks>
      {PLACES.map((place) => (
        <Button key={place.at} onClick={() => onMark(place.at)} disabled={!ready} describedBy="teach-rest">
          {t(place.button)}
        </Button>
      ))}
    </div>
    {!ready && <Note id="teach-rest">{t('teach.waitStill')}</Note>}
  </>
);

interface TeachCardProps {
  teach: TeachStatus | null;
  roof: RoofStatus;
  config: RoofConfig;
  checks: ComponentChildren;
  blocked: string; // why a jog can't start now ('' when it can, apart from the confirmation)
  onChanged: () => void;
}

const TeachCard: FunctionalComponent<TeachCardProps> = ({ teach, roof, config, checks, blocked, onChanged }) => {
  const [attended, setAttended] = useState(false);
  const [problem, setProblem] = useState('');
  const [outcome, setOutcome] = useState<Outcome | null>(null);
  const jog = useJog((text) => setProblem(text || t('teach.jogEnded')));
  const moving = roof.state === 'opening' || roof.state === 'closing';
  const press = (direction: JogDirection) => {
    setProblem('');
    setOutcome(null);
    jog.press(direction);
  };
  const mark = async (at: TeachPlace) => {
    const answer = await markTeach(at);
    setProblem(answer.ok ? '' : answer.text || t('teach.notMarked'));
    setOutcome(null);
    onChanged();
  };
  const done = (next: Outcome) => {
    setOutcome(next);
    onChanged();
  };
  return (
    <SettingsCard id="teach" title={t('teach.title')} hint={t('teach.hint')}>
      <Note>{t('teach.intro')}</Note>
      {checks}
      <Toggle
        dataField="teach.attended"
        label={t('teach.attended')}
        hint={t('teach.attendedHint')}
        checked={attended}
        onChange={setAttended}
      />
      <ReadingRow label={t('calibrate.hallNow')} value={String(roof.hall.counts)} />
      <HallScale counts={roof.hall.counts} low={config.hallLowCounts} high={config.hallHighCounts} />
      <JogControls
        why={blocked || (attended ? '' : t('calibrate.reason.notAttended'))}
        moving={moving}
        held={jog.active}
        onPress={press}
        onRelease={jog.release}
      />
      {problem && <Note tone="bad">{problem}</Note>}
      <Marks teach={teach} ready={teach?.rest.ready === true && !moving} onMark={(at) => void mark(at)} />
      {teach?.proposal && <Proposal proposal={teach.proposal} config={config} onDone={done} />}
      {outcome && <Note tone={outcome.tone}>{outcome.text}</Note>}
      <Note>{t('teach.next')}</Note>
    </SettingsCard>
  );
};

export default TeachCard;
