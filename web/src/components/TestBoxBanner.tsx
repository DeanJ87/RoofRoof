import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { route } from 'preact-router';
import { Note } from './ui';
import { TEST_BOX_CHANGED, fetchStatus } from '../lib/settingsApi';
import type { RoofTestBox } from '../types';
import { formatDateTime } from '../i18n/format';
import { t } from '../i18n';

// Spec 022 (constitution 3.0.0): while a test-box bypass is set, every page
// says so in red, naming each bypass in effect and when it ends, or that they
// expired. Read every 5 s, and at once after a settings save on this page
// (TEST_BOX_CHANGED), so it can't be missed or out of date for long.

const POLL_MS = 5000;

const bypassNames = (box: RoofTestBox) =>
  [box.park && t('testBox.banner.park'), box.weather && t('testBox.banner.weather'), box.daylight && t('testBox.banner.daylight')]
    .filter(Boolean)
    .join(', ');

/** The banner's text, or null when nothing is set. */
export const testBoxText = (box: RoofTestBox | undefined): string | null => {
  if (!box?.set) return null;
  const until = formatDateTime(new Date(box.until * 1000), { dateStyle: 'medium', timeStyle: 'short' });
  if (box.waitingForClock) return t('testBox.banner.waitingForClock');
  if (box.expired) return t('testBox.banner.expired', { until });
  if (!box.active) return t('testBox.banner.notInEffect');
  return t('testBox.banner.active', { bypasses: bypassNames(box), until });
};

const TestBoxBanner: FunctionalComponent = () => {
  const [box, setBox] = useState<RoofTestBox | undefined>(undefined);
  useEffect(() => {
    let live = true;
    const read = () =>
      fetchStatus()
        .then((status) => live && setBox(status?.roof?.testBox))
        .catch(() => undefined);
    read();
    const timer = setInterval(read, POLL_MS);
    window.addEventListener(TEST_BOX_CHANGED, read);
    return () => {
      live = false;
      clearInterval(timer);
      window.removeEventListener(TEST_BOX_CHANGED, read);
    };
  }, []);
  const text = testBoxText(box);
  if (text === null) return null;
  return (
    <div class="bench-banner test-box-banner" role="status" data-inventory="test-box-banner">
      <Note tone="bad" action={{ label: t('testBox.banner.settings'), onClick: () => route('/settings?tab=roof') }}>
        {text}
      </Note>
    </div>
  );
};

export default TestBoxBanner;
