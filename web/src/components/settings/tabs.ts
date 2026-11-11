import { t } from '../../i18n';
export type SettingsTabId = 'device' | 'roof' | 'network' | 'time' | 'alerts';

export const SETTINGS_TABS: { id: SettingsTabId; label: string }[] = [
  { id: 'device', label: t('settings.tabs.device') },
  { id: 'roof', label: t('settings.tabs.roof') },
  { id: 'network', label: t('settings.tabs.network') },
  { id: 'time', label: t('settings.tabs.time') },
  { id: 'alerts', label: t('settings.tabs.alerts') },
];

// Older links (/settings?section=alpaca from the Alpaca setup redirect,
// section anchors used elsewhere in the UI) map onto the tab that now holds
// that section.
const SECTION_TO_TAB: Record<string, SettingsTabId> = {
  alpaca: 'network',
  alerts: 'alerts',
  mqtt: 'network',
  wifi: 'network',
  security: 'device',
  roof: 'roof',
  hall: 'roof',
  safety: 'roof',
  park: 'roof',
  daylight: 'roof',
  autoClose: 'roof',
  diagnostics: 'device',
};

export const isSettingsTab = (value: string | null): value is SettingsTabId => SETTINGS_TABS.some((tab) => tab.id === value);

// The query string, also when it's inside a hash route (the demo uses
// #/settings?tab=network).
// With hash routing the query is in the hash, and wins over the page's own
// query (the demo's own query parameters, if it adds any).
export const locationQuery = (location: Pick<Location, 'search' | 'hash'>) =>
  location.hash.includes('?') ? location.hash.slice(location.hash.indexOf('?')) : location.search;

export const tabFromLocation = (search: string): { tab: SettingsTabId; anchor?: string } => {
  const params = new URLSearchParams(search);
  const tab = params.get('tab');
  const section = params.get('section') ?? undefined;
  if (isSettingsTab(tab)) return { tab, anchor: section };
  if (section && SECTION_TO_TAB[section]) return { tab: SECTION_TO_TAB[section], anchor: section };
  return { tab: 'device' };
};

// Which tab owns a validation error path (e.g. "alerts.ntfy.topic"); anything else is on Device.
const ERROR_ROOT_TAB: Record<string, SettingsTabId> = {
  wifi: 'network',
  mqtt: 'network',
  mqttBroker: 'network',
  mqttPort: 'network',
  mqttTopic: 'network',
  mqttInterval: 'network',
  ntp: 'time',
  timezone: 'time',
  alpaca: 'network',
  alerts: 'alerts',
  roof: 'roof',
  safety: 'roof',
  park: 'roof',
};

export const tabForErrorPath = (path: string): SettingsTabId => {
  const root = path.split('.')[0];
  return Object.prototype.hasOwnProperty.call(ERROR_ROOT_TAB, root) ? ERROR_ROOT_TAB[root] : 'device';
};
