import { describe, expect, it } from 'vitest';
import { SCREENS, firstScreenOf, stepOf } from '../screens';
import { WIZARD_PAGES, pageIndex } from '../pages';

// Spec 020 FR-021: the SQMeter screen comes first on the Site and time page;
// the pages, and so `setup.step`, don't change.

describe('wizard screens', () => {
  it('starts the Site and time page with SQMeter, then the site and the time', () => {
    expect(SCREENS.filter((s) => s.page === 'site').map((s) => s.id)).toEqual(['sqmeter', 'site', 'time']);
    expect(SCREENS[firstScreenOf(pageIndex('site'))].id).toBe('sqmeter');
  });

  it('keeps the pages and their steps', () => {
    expect(WIZARD_PAGES).toHaveLength(12);
    expect(stepOf(SCREENS.findIndex((s) => s.id === 'sqmeter'))).toBe(1);
    expect(SCREENS.map((s) => s.id).slice(0, 5)).toEqual(['name', 'password', 'sqmeter', 'site', 'time']);
  });

  it('never makes the SQMeter screen a choice or holds Continue', () => {
    const sqmeter = SCREENS.find((s) => s.id === 'sqmeter')!;
    expect(sqmeter.choice).toBeUndefined();
    expect(sqmeter.answered).toBeUndefined();
    expect(sqmeter.shows).toBeUndefined();
  });
});
