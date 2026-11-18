import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import type { RoofInputs } from '../../types';
import type { WizardPageProps } from './context';
import { reviewChecks, type CheckState } from './review';
import { pageIndex } from './pages';
import { fetchInputs } from '../../lib/setupApi';
import { docsLink } from '../../lib/docsLinks';
import { StatusBadge } from '../settings/controls';
import { Note } from '../ui';
import { t } from '../../i18n';

// The review (spec 015 FR-010): every check with its state and what to do.

const TONE: Record<CheckState, 'ok' | 'warn' | 'bad' | 'off'> = { ok: 'ok', warn: 'warn', bad: 'bad', unknown: 'off' };
const LABEL: Record<CheckState, string> = {
  ok: t('wizard.review.ok'),
  warn: t('wizard.review.attention'),
  bad: t('wizard.review.problem'),
  unknown: t('wizard.review.unknown'),
};
const NOTE_TONE: Record<CheckState, 'ok' | 'warn' | 'bad' | 'muted'> = { ok: 'ok', warn: 'warn', bad: 'bad', unknown: 'muted' };

// The device's latest reading of the sources, every 2 s while the screen is open.
const INPUTS_EVERY_MS = 2000;
const useInputs = () => {
  const [inputs, setInputs] = useState<RoofInputs | null>(null);
  useEffect(() => {
    const read = () => void fetchInputs().then((doc) => doc && setInputs(doc));
    read();
    const timer = setInterval(read, INPUTS_EVERY_MS);
    return () => clearInterval(timer);
  }, []);
  return inputs;
};

export const ReviewScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const inputs = useInputs();
  const checks = reviewChecks({ config: props.config, status: props.status, inputs, hallNotFitted: props.hallNotFitted });
  return (
    <ul class="wizard-checks" aria-label={t('wizard.review.title')}>
      {checks.map((check) => (
        <li key={check.id} data-check={check.id} data-state={check.state}>
          <div class="reading-row">
            <span class="reading-label">{check.label}</span>
            <StatusBadge tone={TONE[check.state]} label={LABEL[check.state]} />
          </div>
          <Note
            tone={NOTE_TONE[check.state]}
            action={{ label: t('wizard.review.change'), onClick: () => props.goToPage(pageIndex(check.page)) }}
          >
            {check.text}
          </Note>
        </li>
      ))}
    </ul>
  );
};

// After Finish (spec 015 FR-011, spec 018 FR-016): where the motion tests
// happen, the attended commissioning, never here.
export const DoneScreen: FunctionalComponent = () => (
  <div class="card-group" data-finished>
    <Note tone="ok">{t('wizard.finished')}</Note>
    <Note>{t('wizard.review.motionTestsThere')}</Note>
    <Note>
      <a href={docsLink('hardware/commissioning.md')} target="_blank" rel="noopener noreferrer">
        {t('wizard.review.commissioningLink')}
      </a>
    </Note>
    <Note>
      <a href={docsLink('hardware/commissioning.md', 'stage-11-the-gate-for-unattended-use')} target="_blank" rel="noopener noreferrer">
        {t('wizard.review.gateLink')}
      </a>
    </Note>
  </div>
);
