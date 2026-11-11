import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useState } from 'preact/hooks';
import type { CalibrationStatus, CalibrationWhere, RoofConfig, RoofStatus, SystemStatus } from '../types';
import { useWebSocket } from '../hooks/useWebSocket';
import { useCalibration } from '../hooks/useCalibration';
import { useConfigEditor } from '../hooks/useConfigEditor';
import { abortCalibration, confirmCalibration, startCalibration } from '../lib/calibrationApi';
import {
  acceptedRoof,
  calibrationChecks,
  proposalRows,
  settingsChangedSince,
  startBlocked,
  whereFromState,
  type CalibrationCheck,
} from '../lib/calibrationView';
import { LIVE_MS } from '../lib/roofView';
import { Field, NumberInput, SelectInput, SettingsCard, StatusBadge, Toggle } from './settings/controls';
import { Button, Note, ProgressMeter, ReadingRow } from './ui';
import HallScale from '../dashboard/HallScale';
import { t } from '../i18n';
import { formatNumber } from '../i18n/format';
import { deviceText } from '../i18n/deviceMessage';

// The attended calibration run (spec 016): what it needs, the start form, the
// run with Stop always shown, and the result with the proposal beside the
// saved values. Nothing is saved until the person accepts, and then through
// the settings save like Settings. The device checks everything again.

const seconds = (ms: number) => formatNumber(ms / 1000, 1);

const Checks: FunctionalComponent<{ checks: CalibrationCheck[] }> = ({ checks }) => (
  <div data-calibrate-checks>
    {checks.map((check) => (
      <div class="reading-row" key={check.id}>
        <span>{check.label}</span>
        <StatusBadge tone={check.ok ? 'ok' : 'bad'} label={check.ok ? t('calibrate.ok') : check.text || t('calibrate.notReady')} />
      </div>
    ))}
  </div>
);

interface StartProps {
  roof: RoofStatus;
  config: RoofConfig;
  live: boolean;
  onStarted: () => void;
}

const StartForm: FunctionalComponent<StartProps> = ({ roof, config, live, onStarted }) => {
  const [attended, setAttended] = useState(false);
  const [at, setAt] = useState<CalibrationWhere>(whereFromState(roof));
  const [travel, setTravel] = useState(config.travelMm);
  const [topPass, setTopPass] = useState(false);
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState('');
  const checks = calibrationChecks(roof);
  const blocked = startBlocked(checks, attended, live);
  const canTop = config.topDutyPercent > config.slowDutyPercent;
  const start = async () => {
    setBusy(true);
    const answer = await startCalibration({ attended, at, travelMm: travel, topPass: topPass && canTop });
    setBusy(false);
    setProblem(answer.ok ? '' : answer.text || t('calibrate.refused'));
    if (answer.ok) onStarted();
  };
  return (
    <SettingsCard id="calibrate-start" title={t('calibrate.startTitle')} hint={t('calibrate.startHint')}>
      <Checks checks={checks} />
      <Toggle
        dataField="calibrate.attended"
        label={t('calibrate.attended')}
        hint={t('calibrate.attendedHint')}
        checked={attended}
        onChange={setAttended}
      />
      <div class="form-grid">
        <Field label={t('calibrate.where')} hint={t('calibrate.whereHint')}>
          <SelectInput
            dataField="calibrate.at"
            value={at}
            options={[
              { value: 'closed', label: t('calibrate.at.closed') },
              { value: 'open', label: t('calibrate.at.open') },
              { value: 'partWay', label: t('calibrate.at.partWay') },
            ]}
            onChange={(v) => setAt(v as CalibrationWhere)}
          />
        </Field>
        <Field label={t('calibrate.travel')} hint={t('calibrate.travelHint')}>
          <NumberInput dataField="calibrate.travelMm" value={travel} min={100} unit="mm" integer onChange={setTravel} />
        </Field>
      </div>
      {canTop && (
        <Toggle
          dataField="calibrate.topPass"
          label={t('calibrate.topPass')}
          hint={t('calibrate.topPassHint', { duty: config.topDutyPercent })}
          checked={topPass}
          onChange={setTopPass}
        />
      )}
      <div class="btn-row">
        <Button variant="primary" onClick={() => void start()} disabled={blocked !== ''} busy={busy} describedBy="calibrate-why">
          {t('calibrate.start')}
        </Button>
      </div>
      {blocked && <Note id="calibrate-why">{blocked}</Note>}
      {problem && <Note tone="bad">{problem}</Note>}
    </SettingsCard>
  );
};

type Key = Parameters<typeof t>[0];

const PHASE_TEXT: Record<string, Key> = {
  wait: 'calibrate.waiting',
  settle: 'calibrate.settling',
  confirm: 'calibrate.confirming',
  drive: 'calibrate.settling',
};

const LEG_TEXT: Record<string, Key> = {
  toClosed: 'calibrate.leg.toClosed',
  slowOpen: 'calibrate.leg.slowOpen',
  slowClose: 'calibrate.leg.slowClose',
  topOpen: 'calibrate.leg.topOpen',
  topClose: 'calibrate.leg.topClose',
};

// The run under way, with Stop: never disabled, never busy (constitution VII).
// After the slow open: only the person can see that the roof went the right way.
const ConfirmOpen: FunctionalComponent<{ onAnswer: () => void }> = ({ onAnswer }) => {
  const answer = (atOpenEnd: boolean) => void confirmCalibration(atOpenEnd).then(onAnswer);
  return (
    <div data-calibrate-confirm>
      <Note tone="warn">{t('calibrate.confirmQuestion')}</Note>
      <div class="btn-row">
        <Button variant="primary" onClick={() => answer(true)}>
          {t('calibrate.confirmYes')}
        </Button>
        <Button onClick={() => answer(false)}>{t('calibrate.confirmNo')}</Button>
      </div>
    </div>
  );
};

interface RunningProps {
  run: CalibrationStatus;
  roof: RoofStatus;
  config: RoofConfig;
  onStop: () => void;
  onAnswer: () => void;
}

const Running: FunctionalComponent<RunningProps> = ({ run, roof, config, onStop, onAnswer }) => {
  const leg = run.leg;
  const driving = leg?.phase === 'drive' && leg.limitMs > 0;
  return (
    <SettingsCard id="calibrate-run" title={t('calibrate.runningTitle')}>
      {leg && (
        <Note>
          {t('calibrate.legOf', { index: leg.index, count: leg.count, leg: t(LEG_TEXT[leg.name]) })}{' '}
          {driving ? t('calibrate.driving', { elapsed: seconds(leg.elapsedMs), limit: seconds(leg.limitMs) }) : t(PHASE_TEXT[leg.phase])}
        </Note>
      )}
      {leg?.phase === 'confirm' && <ConfirmOpen onAnswer={onAnswer} />}
      {driving && <ProgressMeter value={(leg.elapsedMs / leg.limitMs) * 100} label={t('calibrate.limitUsed')} />}
      <ReadingRow label={t('calibrate.hallNow')} value={String(roof.hall.counts)} />
      <HallScale counts={roof.hall.counts} low={config.hallLowCounts} high={config.hallHighCounts} />
      <div class="btn-row">
        <Button variant="danger" onClick={onStop}>
          {t('calibrate.stop')}
        </Button>
      </div>
    </SettingsCard>
  );
};

const legValue = (run: CalibrationStatus, index: number) => {
  const leg = run.legs[index];
  const amps =
    leg.ampsMean === null || leg.ampsPeak === null
      ? ''
      : ` · ${t('calibrate.amps', { mean: formatNumber(leg.ampsMean, 1), peak: formatNumber(leg.ampsPeak, 1) })}`;
  return `${seconds(leg.drivenMs)} s · ${formatNumber(leg.speedMmPerSecond, 1)} mm/s${amps}`;
};

const Readings: FunctionalComponent<{ run: CalibrationStatus }> = ({ run }) => {
  const { between, closed, open } = run.hall;
  return (
    <div data-calibrate-readings>
      {run.legs.map((leg, index) => (
        <ReadingRow key={leg.name} label={t(LEG_TEXT[leg.name])} value={legValue(run, index)} />
      ))}
      {between && (
        <ReadingRow
          label={t('calibrate.between')}
          value={t('calibrate.betweenValue', { mean: formatNumber(between.mean, 0), noise: formatNumber(between.noise, 1) })}
        />
      )}
      {closed && <ReadingRow label={t('calibrate.closedEnd')} value={formatNumber(closed.mean, 0)} />}
      {open && <ReadingRow label={t('calibrate.openEnd')} value={formatNumber(open.mean, 0)} />}
    </div>
  );
};

const ROW_LABEL: Record<string, Key> = {
  travelMm: 'settings.roof.travel',
  speedMmPerSecond: 'settings.roof.speed',
  timeoutMarginSeconds: 'settings.roof.timeoutMargin',
  hallHighEnd: 'settings.roof.hallHighEnd',
  hallHighCounts: 'settings.roof.hallHigh',
  hallLowCounts: 'settings.roof.hallLow',
};

interface ResultProps {
  run: CalibrationStatus;
  config: RoofConfig;
  accept: (roof: RoofConfig) => Promise<string | null>;
}

const Result: FunctionalComponent<ResultProps> = ({ run, config, accept }) => {
  const [busy, setBusy] = useState(false);
  const [outcome, setOutcome] = useState<{ tone: 'ok' | 'bad'; text: string } | null>(null);
  const proposal = run.proposal;
  const save = async () => {
    if (!proposal) return;
    setBusy(true);
    const problem = await accept(acceptedRoof(proposal, config));
    setBusy(false);
    setOutcome(problem ? { tone: 'bad', text: problem } : { tone: 'ok', text: t('calibrate.saved') });
  };
  return (
    <SettingsCard id="calibrate-result" title={t('calibrate.resultTitle')}>
      {run.ending && <Note tone={run.state === 'done' ? 'ok' : 'bad'}>{deviceText(run.ending.text)}</Note>}
      <Readings run={run} />
      {proposal && (
        <>
          <h3 class="card-group-title">{t('calibrate.proposal')}</h3>
          {proposalRows(proposal, config).map((row) => (
            <ReadingRow
              key={row.key}
              label={t(ROW_LABEL[row.key])}
              value={
                row.proposed === null
                  ? t('calibrate.withheld', { saved: row.saved })
                  : t(row.changed ? 'calibrate.change' : 'calibrate.same', { saved: row.saved, proposed: row.proposed })
              }
            />
          ))}
          {[proposal.speedProblem, proposal.hallProblem, ...proposal.warnings].map(
            (problem) =>
              problem && (
                <Note tone="warn" key={problem.reason}>
                  {deviceText(problem.text)}
                </Note>
              ),
          )}
          {settingsChangedSince(proposal, config) && <Note tone="warn">{t('calibrate.changedSince')}</Note>}
          {outcome ? (
            <Note tone={outcome.tone}>{outcome.text}</Note>
          ) : (
            <div class="btn-row">
              <Button variant="primary" onClick={() => void save()} busy={busy} busyLabel={t('common.saving')}>
                {t('calibrate.accept')}
              </Button>
              <Button variant="ghost" onClick={() => setOutcome({ tone: 'ok', text: t('calibrate.discarded') })}>
                {t('calibrate.discard')}
              </Button>
            </div>
          )}
        </>
      )}
    </SettingsCard>
  );
};

// Live status recent enough to start a run on.
const isFresh = (live: SystemStatus | null, connected: boolean, lastMessageAt: number | null | undefined) =>
  Boolean(live && connected && typeof lastMessageAt === 'number' && Date.now() - lastMessageAt <= LIVE_MS);

const Calibrate: FunctionalComponent = () => {
  const editor = useConfigEditor();
  const { data: live, connected, lastMessageAt } = useWebSocket<SystemStatus>('/ws/status');
  const { status: run, refresh } = useCalibration();
  const roof = live?.roof ?? editor.status?.roof;
  const config = editor.saved?.roof;
  if (editor.loading) return <div class="empty-state">{t('settings.loadingSettings')}</div>;
  if (!config || !roof) return <div class="empty-state tone-red">{t('calibrate.noDevice')}</div>;
  const accept = async (next: RoofConfig) => {
    const outcome = await editor.save({ roof: next });
    if (outcome.kind === 'saved') return null;
    return outcome.kind === 'refused' ? outcome.message : t('calibrate.notSaved');
  };
  const stop = () => void abortCalibration().then(refresh);
  const fresh = isFresh(live, connected, lastMessageAt);
  return (
    <div class="panel-page settings-page page-enter" data-calibrate-page>
      <h2>{t('calibrate.title')}</h2>
      <Note>{t('calibrate.intro')}</Note>
      {run?.active ? (
        <Running run={run} roof={roof} config={config} onStop={stop} onAnswer={refresh} />
      ) : (
        <StartForm roof={roof} config={config} live={fresh} onStarted={refresh} />
      )}
      {run && !run.active && run.state !== 'idle' && <Result run={run} config={config} accept={accept} />}
      <div class="btn-row">
        <Button variant="link" onClick={() => route('/settings?tab=roof')}>
          {t('calibrate.toSettings')}
        </Button>
      </div>
    </div>
  );
};

export default Calibrate;
