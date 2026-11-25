import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { fetchCrashes, fetchDeviceLog, hasRecords } from '../../lib/crashApi';
import { Button, Card, Note } from '../ui';
import { showToast } from '../toast';
import { t } from '../../i18n';

// Spec 029 FR-019: the unexpected restarts the device recorded, as the device
// wrote them, shown whether advanced diagnostics is on or off (crash capture
// always runs). Copy puts them on the clipboard for an issue or for
// tools/decode_crash.py.

const copy = async (text: string | null) => {
  try {
    if (text === null) throw new Error('unreadable');
    await navigator.clipboard.writeText(text);
    showToast({ message: t('diagnostics.crashes.copied') });
  } catch {
    showToast({ message: t('diagnostics.crashes.copyFailed'), tone: 'bad' });
  }
};

const CrashesCard: FunctionalComponent = () => {
  const [text, setText] = useState<string | null | undefined>(undefined);
  useEffect(() => {
    void fetchCrashes().then(setText);
  }, []);
  const records = !!text && hasRecords(text);
  return (
    <Card title={t('diagnostics.crashes.title')} icon="warning" hint={t('diagnostics.crashes.hint')}>
      {text === null && <Note tone="bad">{t('diagnostics.crashes.unreadable')}</Note>}
      {text && !records && <Note>{t('diagnostics.crashes.none')}</Note>}
      {records && <pre class="crash-text ltr mono">{text}</pre>}
      <div class="btn-row">
        {records && (
          <Button small onClick={() => void copy(text)}>
            {t('diagnostics.crashes.copy')}
          </Button>
        )}
        <Button small onClick={() => void fetchDeviceLog().then(copy)}>
          {t('diagnostics.crashes.copyLog')}
        </Button>
      </div>
    </Card>
  );
};

export default CrashesCard;
