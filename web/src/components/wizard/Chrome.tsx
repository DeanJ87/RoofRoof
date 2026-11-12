import { FunctionalComponent } from 'preact';
import { listReasons } from '../settings/restart';
import { restartDevice } from '../../lib/settingsApi';
import { Button, Icon, Note, ProgressMeter } from '../ui';
import { t } from '../../i18n';

// The wizard dialog's own parts (spec 018): its header, footer and leave screen.

// Back, the progress bar and Close (FR-002).
export const Header: FunctionalComponent<{ onBack?: () => void; onClose: () => void; at: [number, number] }> = ({
  onBack,
  onClose,
  at,
}) => (
  <>
    {onBack ? (
      <Button variant="ghost" ariaLabel={t('wizard.back')} onClick={onBack}>
        <Icon name="back" tone="muted" size={20} />
      </Button>
    ) : (
      <span class="modal-header-spacer" />
    )}
    <ProgressMeter
      value={(at[0] / at[1]) * 100}
      label={t('wizard.progress')}
      valueText={t('wizard.progressText', { number: at[0], total: at[1] })}
    />
    <Button variant="ghost" ariaLabel={t('wizard.close')} onClick={onClose}>
      <Icon name="close" tone="muted" size={20} />
    </Button>
  </>
);

// "Leave setup?" (FR-004): keep going, leave for now, or skip with spec 015's warning.
export const LeaveChoices: FunctionalComponent<{
  skipping: boolean;
  busy: boolean;
  keepGoing: () => void;
  leave: () => void;
  askSkip: () => void;
  skip: () => void;
}> = ({ skipping, busy, keepGoing, leave, askSkip, skip }) => (
  <div class="card-group" data-leave>
    {skipping ? <Note tone="bad">{t('wizard.skipWarning')}</Note> : <Note>{t('wizard.leaveKeeps')}</Note>}
    <div class="btn-row">
      <Button variant="primary" onClick={keepGoing}>
        {t('wizard.keepGoing')}
      </Button>
      {skipping ? (
        <Button variant="danger" onClick={skip} busy={busy}>
          {t('wizard.skipConfirm')}
        </Button>
      ) : (
        <>
          <Button onClick={leave}>{t('wizard.leaveForNow')}</Button>
          <Button variant="ghost" onClick={askSkip}>
            {t('wizard.skipSetup')}
          </Button>
        </>
      )}
    </div>
  </div>
);

export const RestartNote: FunctionalComponent<{ restart: string[] }> = ({ restart }) => (
  <Note tone="warn" action={{ label: t('settings.restart'), onClick: () => void restartDevice().catch(() => undefined) }}>
    {t('wizard.restartToApply', { listReasons: listReasons(restart) })}
  </Note>
);

// Skip for now and Continue (FR-002, FR-006): an unavailable Continue says what the screen still needs.
export const Footer: FunctionalComponent<{
  last: boolean;
  answered: boolean;
  missing: string;
  busy: boolean;
  onSkip: () => void;
  onContinue: () => void;
}> = ({ last, answered, missing, busy, onSkip, onContinue }) => (
  <>
    {!answered && <Note id="wizard-missing">{missing}</Note>}
    {!last && (
      <Button variant="ghost" onClick={onSkip}>
        {t('wizard.skipForNow')}
      </Button>
    )}
    <Button
      variant="primary"
      onClick={onContinue}
      disabled={!answered}
      describedBy={answered ? undefined : 'wizard-missing'}
      busy={busy}
      busyLabel={t('common.saving')}
    >
      {last ? t('wizard.finish') : t('wizard.continue')}
    </Button>
  </>
);
