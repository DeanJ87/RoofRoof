// The wizard's parts in order (spec 025): `setup.step` is an index into this
// list, the point the wizard resumes at. Each part is a short run of screens
// (screens.ts) that starts where an installer would pick up again.
export type WizardPageId =
  'network' | 'board' | 'wiring' | 'testBox' | 'sqmeter' | 'park' | 'direction' | 'teach' | 'calibrate' | 'alerts' | 'password' | 'review';

export const WIZARD_PAGES: WizardPageId[] = [
  'network',
  'board',
  'wiring',
  'testBox',
  'sqmeter',
  'park',
  'direction',
  'teach',
  'calibrate',
  'alerts',
  'password',
  'review',
];

export const pageIndex = (id: WizardPageId) => WIZARD_PAGES.indexOf(id);

// A step from the URL (?step=1 is the first part) or the device; anything else is part 1.
export const clampStep = (value: unknown): number => {
  const step = Number(value);
  return Number.isInteger(step) && step >= 0 && step < WIZARD_PAGES.length ? step : 0;
};

// The settings N.I.N.A.'s Test compares before and after its save.
export const NINA_FIELDS = ['park.nina.url'];
