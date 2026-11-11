import { t } from '../../i18n';

// The wizard's pages in order (spec 015). `setup.step` is an index into this list.
export type WizardPageId =
  'device' | 'site' | 'hardware' | 'hall' | 'calibrate' | 'motion' | 'weather' | 'nina' | 'alerts' | 'daylight' | 'review';

export const WIZARD_PAGES: { id: WizardPageId; title: string }[] = [
  { id: 'device', title: t('wizard.pages.device') },
  { id: 'site', title: t('wizard.pages.site') },
  { id: 'hardware', title: t('wizard.pages.hardware') },
  { id: 'hall', title: t('wizard.pages.hall') },
  { id: 'calibrate', title: t('wizard.pages.calibrate') },
  { id: 'motion', title: t('wizard.pages.motion') },
  { id: 'weather', title: t('wizard.pages.weather') },
  { id: 'nina', title: t('wizard.pages.nina') },
  { id: 'alerts', title: t('wizard.pages.alerts') },
  { id: 'daylight', title: t('wizard.pages.daylight') },
  { id: 'review', title: t('wizard.pages.review') },
];

export const pageIndex = (id: WizardPageId) => WIZARD_PAGES.findIndex((page) => page.id === id);

// A step from the URL (?step=1 is the first page) or the device; anything else is page 1.
export const clampStep = (value: unknown): number => {
  const step = Number(value);
  return Number.isInteger(step) && step >= 0 && step < WIZARD_PAGES.length ? step : 0;
};

// The settings each page edits: a Test compares them before and after its save.
export const WEATHER_FIELDS = ['safety.http.enabled', 'safety.http.url', 'safety.mqtt.enabled', 'safety.mqtt.topic'];
export const NINA_FIELDS = ['park.nina.url'];
