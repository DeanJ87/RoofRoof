import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { SafetyConfig } from '../../types';
import { ActionButton } from './controls';
import { Note } from '../ui';
import { testSqmeter, type TestAnswer } from '../../lib/sqmeterCopy';
import { sqmeterAddressError } from '../../lib/sqmeterAddress';
import { t } from '../../i18n';

// Spec 020 FR-038: Test reads SQMeter's /api/safety once at the address and
// login as typed (saved or not), through the device, and says what came back:
// safe, unsafe, or why not. Nothing is saved.
export const SqmeterAddressTest: FunctionalComponent<{ safety: SafetyConfig }> = ({ safety }) => {
  const [busy, setBusy] = useState(false);
  const [answer, setAnswer] = useState<TestAnswer | null>(null);
  const http = safety.http;
  const usable = http.url.trim() !== '' && sqmeterAddressError(http.url) === null;
  const run = async () => {
    setBusy(true);
    setAnswer(null);
    setAnswer(await testSqmeter(http.url, { username: http.username, password: http.password }));
    setBusy(false);
  };
  return (
    <div class="btn-row">
      <ActionButton onClick={run} disabled={!usable} busy={busy} busyLabel={t('common.testing')}>
        {t('settings.roof.sqmeterTest')}
      </ActionButton>
      <div role="status" aria-live="polite">
        {answer?.ok && <Note tone={answer.safe ? 'ok' : 'warn'}>{answer.text}</Note>}
        {answer && !answer.ok && <Note tone="bad">{answer.error}</Note>}
      </div>
    </div>
  );
};
