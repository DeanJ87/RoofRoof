import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { Note } from './ui';
import { fetchStatus } from '../lib/settingsApi';
import { t } from '../i18n';

// Spec 011 FR-026: bench firmware says so on every page. A bench board's roof
// is simulated and its motor driver's pins stay low; a person must never take
// it for the roof. The build can't change while the page is open, so the
// status is read once (again after a failed read).

const RETRY_MS = 10000;

const BenchBanner: FunctionalComponent = () => {
  const [bench, setBench] = useState(false);
  useEffect(() => {
    let timer: ReturnType<typeof setTimeout> | undefined;
    let live = true;
    const read = () =>
      fetchStatus()
        .then((status) => live && setBench(status?.roof?.simulation === 'bench'))
        .catch(() => {
          if (live) timer = setTimeout(read, RETRY_MS);
        });
    read();
    return () => {
      live = false;
      if (timer) clearTimeout(timer);
    };
  }, []);
  if (!bench) return null;
  return (
    <div class="bench-banner" role="status" data-inventory="bench-banner">
      <Note tone="warn">{t('layout.benchMode')}</Note>
    </div>
  );
};

export default BenchBanner;
