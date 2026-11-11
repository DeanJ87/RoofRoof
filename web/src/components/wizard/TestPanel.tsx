import { FunctionalComponent } from 'preact';
import type { InputsSource, RoofInputs } from '../../types';
import { useInputsTest, type TestSave } from '../../hooks/useInputsTest';
import type { TestPart } from './inputsTest';
import { ActionButton, StatusBadge } from '../settings/controls';
import { Note, ReadingRow } from '../ui';
import { t } from '../../i18n';
import { formatAgeMs } from '../../i18n/format';

// Test on the weather and N.I.N.A. pages (spec 015 FR-009): the answer the
// device's own poll brings back after the page is saved, in the card that
// started it (DS-06).

const SOURCE_STATE = {
  off: 'wizard.test.state.off',
  waiting: 'wizard.test.state.waiting',
  good: 'wizard.test.state.good',
  stale: 'wizard.test.state.stale',
  unreachable: 'wizard.test.state.unreachable',
  unreadable: 'wizard.test.state.unreadable',
  offline: 'wizard.test.state.offline',
} as const;

const NINA_STATE = {
  notConfigured: 'wizard.test.state.notConfigured',
  waiting: 'wizard.test.state.waiting',
  good: 'wizard.test.state.answering',
  unreachable: 'wizard.test.state.unreachable',
  unreadable: 'wizard.test.state.unreadable',
  apiError: 'wizard.test.state.apiError',
} as const;

const VERDICT = {
  safe: ['ok', 'wizard.test.safe'],
  unsafe: ['bad', 'wizard.test.unsafe'],
  unknown: ['warn', 'wizard.test.unknown'],
} as const;

const yesNo = (value: boolean) => (value ? t('wizard.test.yes') : t('wizard.test.no'));

const SourceRows: FunctionalComponent<{ label: string; source: InputsSource }> = ({ label, source }) =>
  source.enabled ? (
    <>
      <ReadingRow label={label} value={t(SOURCE_STATE[source.state])} />
      {source.text && <Note tone={source.state === 'good' ? 'muted' : 'warn'}>{source.text}</Note>}
    </>
  ) : null;

export const WeatherResult: FunctionalComponent<{ doc: RoofInputs }> = ({ doc }) => {
  const [tone, label] = VERDICT[doc.weather.verdict];
  return (
    <div data-test-result="weather">
      <div class="reading-row">
        <span class="reading-label">{t('wizard.test.verdict')}</span>
        <StatusBadge tone={tone} label={t(label)} />
      </div>
      <SourceRows label={t('settings.roof.sqmeterHttp')} source={doc.weather.http} />
      <SourceRows label={t('settings.roof.sqmeterMqtt')} source={doc.weather.mqtt} />
      {doc.weather.verdict !== 'safe' && <Note tone="warn">{doc.weather.text}</Note>}
    </div>
  );
};

export const NinaResult: FunctionalComponent<{ doc: RoofInputs }> = ({ doc }) => {
  const { nina } = doc;
  const good = nina.state === 'good';
  return (
    <div data-test-result="nina">
      <div class="reading-row">
        <span class="reading-label">N.I.N.A.</span>
        <StatusBadge tone={good ? 'ok' : 'bad'} label={t(NINA_STATE[nina.state])} />
      </div>
      {!good && nina.text && <Note tone="warn">{nina.text}</Note>}
      {good && (
        <>
          <ReadingRow label={t('wizard.test.mountConnected')} value={yesNo(nina.connected)} />
          <ReadingRow label={t('wizard.test.atPark')} value={yesNo(nina.atPark)} />
          <ReadingRow label={t('wizard.test.slewing')} value={yesNo(nina.slewing)} />
          <ReadingRow label={t('wizard.test.readingAge')} value={formatAgeMs(nina.ageMs)} />
        </>
      )}
    </div>
  );
};

export const TestPanel: FunctionalComponent<{ part: TestPart; save: TestSave; dirty: boolean }> = ({ part, save, dirty }) => {
  const { state, run } = useInputsTest(part);
  const Result = part === 'weather' ? WeatherResult : NinaResult;
  const running = state.phase === 'running';
  return (
    <div class="card-group" aria-live="polite">
      <div class="btn-row">
        <ActionButton onClick={() => void run(save)} busy={running} busyLabel={t('common.testing')}>
          {dirty ? t('wizard.test.saveAndTest') : t('wizard.test.test')}
        </ActionButton>
      </div>
      {running && <Note>{t('wizard.test.waitingForReading')}</Note>}
      {state.phase === 'failed' && <Note tone="bad">{state.message}</Note>}
      {state.phase === 'timeout' && <Note tone="warn">{t('wizard.test.noNewReading')}</Note>}
      {state.phase === 'done' && <Result doc={state.doc} />}
    </div>
  );
};
