import { FunctionalComponent } from 'preact';
import { route, useRouter } from 'preact-router';
import { useEffect, useState } from 'preact/hooks';
import type { SetupConfig } from '../types';
import { fetchSetup, saveSetup } from '../lib/setupApi';
import { fetchStatus } from '../lib/settingsApi';
import { showToast } from './toast';
import { Button, Note } from './ui';
import { t } from '../i18n';

// The setup wizard on the dashboard (spec 015 FR-003, FR-004): a new device
// opens it on the first dashboard visit of a page load (not from the setup
// hotspot); after that, and on an upgraded device, a banner offers it.
// Dismiss sets the setup done. Firmware without a setup state shows nothing.

let openedThisLoad = false; // module state: resets with the page

/** For tests: forget that the wizard has been opened in this page load. */
export const resetSetupPrompt = () => {
  openedThisLoad = false;
};

const SetupPrompt: FunctionalComponent = () => {
  const [router] = useRouter();
  const onDashboard = router.url.split('?')[0] === '/';
  const [setup, setSetup] = useState<SetupConfig | null>(null);

  useEffect(() => {
    if (!onDashboard) return;
    let live = true;
    void Promise.all([fetchSetup(), fetchStatus().catch(() => null)]).then(([current, status]) => {
      if (!live || !current) return;
      if (current.state === 'new' && !openedThisLoad && status && !status.wifi.apMode) {
        openedThisLoad = true;
        route('/wizard');
        return;
      }
      setSetup(current);
    });
    return () => {
      live = false;
    };
  }, [onDashboard]);

  if (!onDashboard || !setup || setup.state === 'done') return null;
  const fresh = setup.state === 'new';
  const dismiss = async () => {
    if (await saveSetup({ state: 'done', step: setup.step })) setSetup({ ...setup, state: 'done' });
    else showToast({ message: t('setup.dismissFailed'), tone: 'bad' });
  };
  return (
    <div class="setup-banner" data-inventory="setup-banner" data-setup-state={setup.state}>
      <Note tone={fresh ? 'warn' : 'muted'}>{fresh ? t('setup.notFinished') : t('setup.offered')}</Note>
      <div class="btn-row">
        <Button small variant="primary" onClick={() => route('/wizard')}>
          {fresh ? t('setup.continue') : t('setup.run')}
        </Button>
        <Button small variant="ghost" onClick={() => void dismiss()}>
          {t('setup.dismiss')}
        </Button>
      </div>
    </div>
  );
};

export default SetupPrompt;
