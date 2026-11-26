import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { route, useRouter } from 'preact-router';
import { Banner } from './ui';
import { TEST_BOX_CHANGED } from '../lib/settingsApi';
import { useLiveStatus } from '../hooks/useLiveStatus';
import type { RoofTestBox } from '../types';
import { formatDateTime } from '../i18n/format';
import { listReasons } from './settings/restart';
import { t } from '../i18n';

// Spec 022 (constitution 3.0.0): while a test-box bypass is set, every page
// says so, naming each bypass in effect and when it ends, or that they
// expired. Red while a check is bypassed, amber while every check applies.
// From the page's live status (spec 045); without it, read every 5 s while one
// is set (30 s otherwise), and at once after a settings save (TEST_BOX_CHANGED).

// Every 5 s while a bypass is set, every 30 s otherwise (and at once after a save).
const POLL_SET_MS = 5000;
const POLL_IDLE_MS = 30000;

/** Where the test box is managed (its own page). */
export const TEST_BOX_PATH = '/test-box';

/** The deadline, in this browser's format: weekday, day, month and time. */
export const formatUntil = (until: number) =>
  formatDateTime(new Date(until * 1000), { weekday: 'short', day: 'numeric', month: 'short', hour: 'numeric', minute: '2-digit' });

/** Each bypass in effect now, by its short name. */
export const bypassNames = (box: RoofTestBox): string[] =>
  [box.park && t('testBox.banner.park'), box.weather && t('testBox.banner.weather'), box.daylight && t('testBox.banner.daylight')].filter(
    (name): name is string => Boolean(name),
  );

/** The test box's state, for its page and the Settings entry. */
export type TestBoxMode = 'unknown' | 'off' | 'on' | 'expired' | 'waiting' | 'notStarted' | 'notInEffect';

export const modeOf = (live: RoofTestBox | undefined): TestBoxMode => {
  if (!live) return 'unknown'; // the status isn't read yet
  if (!live.set) return 'off';
  if (live.waitingForClock) return 'waiting';
  if (live.expired) return live.until === 0 ? 'notStarted' : 'expired';
  return live.active ? 'on' : 'notInEffect';
};

export const MODE_PILL: Record<TestBoxMode, string> = {
  unknown: 'pill-dim',
  off: 'pill-dim',
  on: 'pill-red',
  expired: 'pill-amber',
  waiting: 'pill-amber',
  notStarted: 'pill-amber',
  notInEffect: 'pill-amber',
};

export interface TestBoxView {
  tone: 'bad' | 'warn';
  title: string;
  detail: string;
}

const stateView = (box: RoofTestBox): TestBoxView => {
  const until = formatUntil(box.until);
  if (box.waitingForClock) return { tone: 'warn', title: t('testBox.banner.waitingTitle'), detail: t('testBox.banner.waitingDetail') };
  if (box.expired && box.until === 0)
    return { tone: 'warn', title: t('testBox.banner.notStartedTitle'), detail: t('testBox.banner.notStartedDetail') };
  if (box.expired) return { tone: 'warn', title: t('testBox.banner.expiredTitle'), detail: t('testBox.banner.expiredDetail', { until }) };
  if (!box.active) return { tone: 'warn', title: t('testBox.banner.notInEffectTitle'), detail: t('testBox.banner.notInEffectDetail') };
  return {
    tone: 'bad',
    title: t('testBox.banner.activeTitle', { bypasses: listReasons(bypassNames(box)) }),
    detail: t('testBox.banner.activeDetail', { until }),
  };
};

/** The banner's headline and detail, or null when nothing is set. Spec 022: in
 * every state it says when unsafe weather would ask a configured N.I.N.A. to park its mount. */
export const testBoxView = (box: RoofTestBox | undefined): TestBoxView | null => {
  if (!box?.set) return null;
  const view = stateView(box);
  return box.parkRequests ? { ...view, detail: `${view.detail} ${t('testBox.banner.parkRequests')}` } : view;
};

/** The live test box state from the status: read every 5 s while set, 30 s otherwise, and after a save. */
export const useTestBoxStatus = () => {
  // Spec 045: from the live status; polled only without it (5 s while set, 30 s otherwise, and after a save).
  const [set, setSet] = useState(false);
  const box = useLiveStatus((status) => status.roof?.testBox, set ? POLL_SET_MS : POLL_IDLE_MS, TEST_BOX_CHANGED);
  const nowSet = Boolean(box?.set);
  useEffect(() => setSet(nowSet), [nowSet]);
  return box;
};

const TestBoxBanner: FunctionalComponent = () => {
  const box = useTestBoxStatus();
  const [router] = useRouter();
  const view = testBoxView(box);
  if (view === null) return null;
  // On the test box page itself the banner has nothing to link to.
  const here = (router?.url ?? '').split('?')[0] === TEST_BOX_PATH;
  return (
    <Banner
      tone={view.tone}
      title={view.title}
      inventory="test-box-banner"
      className="test-box-banner"
      action={
        here
          ? undefined
          : { label: t('testBox.banner.manage'), ariaLabel: t('testBox.banner.manageName'), onClick: () => route(TEST_BOX_PATH) }
      }
    >
      {view.detail}
    </Banner>
  );
};

export default TestBoxBanner;
