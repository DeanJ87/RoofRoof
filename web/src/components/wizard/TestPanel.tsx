import { FunctionalComponent } from 'preact';
import type { RoofInputs } from '../../types';
import { useInputsTest, type TestSave } from '../../hooks/useInputsTest';
import { ActionButton, StatusBadge } from '../settings/controls';
import { Note, ReadingRow } from '../ui';
import { t } from '../../i18n';
import { formatAgeMs } from '../../i18n/format';

// Test on the N.I.N.A. screen (spec 015 FR-009): the answer the device's own
// poll brings back after the screen is saved, in the card that started it
// (DS-06). SQMeter has its own Test on its screen (spec 025).

const NINA_STATE = {
  notConfigured: 'wizard.test.state.notConfigured',
  waiting: 'wizard.test.state.waiting',
  good: 'wizard.test.state.answering',
  unreachable: 'wizard.test.state.unreachable',
  unreadable: 'wizard.test.state.unreadable',
  apiError: 'wizard.test.state.apiError',
} as const;

const yesNo = (value: boolean) => (value ? t('wizard.test.yes') : t('wizard.test.no'));

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

export const TestPanel: FunctionalComponent<{ save: TestSave; dirty: boolean }> = ({ save, dirty }) => {
  const { state, run } = useInputsTest('nina');
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
      {state.phase === 'done' && <NinaResult doc={state.doc} />}
    </div>
  );
};
