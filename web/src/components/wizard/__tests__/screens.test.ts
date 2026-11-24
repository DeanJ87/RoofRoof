import { describe, expect, it } from 'vitest';
import type { WizardPageProps } from '../context';
import { SCREENS, firstScreenOf, nextScreen, previousScreen, screenIndex, stepOf } from '../screens';
import { WIZARD_PAGES } from '../pages';
import { mockConfig, mockStatus } from '../../../mocks/data';

// Spec 025: twelve parts, their screens in order, and which screens show.

const idx = (id: string) => screenIndex(id);
const props = (change: (p: { config: any; status: any }) => void = () => undefined) => {
  const p = { config: structuredClone(mockConfig), status: structuredClone(mockStatus) };
  change(p);
  return p as unknown as WizardPageProps;
};

describe('wizard screens', () => {
  it('lists the screens in the installer’s order', () => {
    expect(SCREENS.map((s) => s.id)).toEqual([
      'name',
      'network',
      'board',
      'wiring',
      'testBox',
      'sqmeter',
      'site',
      'daylight',
      'parkSwitches',
      'nina',
      'direction',
      'teach',
      'calibrate',
      'motion',
      'alerts',
      'password',
      'review',
    ]);
  });

  it('groups the screens under the twelve parts, in order', () => {
    expect(WIZARD_PAGES).toEqual([
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
    ]);
    expect(Object.fromEntries(SCREENS.map((s) => [s.id, s.page]))).toEqual({
      name: 'network',
      network: 'network',
      board: 'board',
      wiring: 'wiring',
      testBox: 'testBox',
      sqmeter: 'sqmeter',
      site: 'sqmeter',
      daylight: 'sqmeter',
      parkSwitches: 'park',
      nina: 'park',
      direction: 'direction',
      teach: 'teach',
      calibrate: 'calibrate',
      motion: 'calibrate',
      alerts: 'alerts',
      password: 'password',
      review: 'review',
    });
  });

  it('opens each part at its first screen, and the screen saves its part as the step', () => {
    const first = [
      'name',
      'board',
      'wiring',
      'testBox',
      'sqmeter',
      'parkSwitches',
      'direction',
      'teach',
      'calibrate',
      'alerts',
      'password',
      'review',
    ];
    expect(WIZARD_PAGES).toHaveLength(12);
    first.forEach((id, part) => {
      expect(SCREENS[firstScreenOf(part)].id).toBe(id);
      expect(stepOf(firstScreenOf(part))).toBe(part);
    });
    expect(stepOf(idx('nina'))).toBe(5);
    expect(stepOf(idx('site'))).toBe(4);
    expect(stepOf(idx('motion'))).toBe(8);
  });

  it('shows the network screen only with Ethernet', () => {
    const network = SCREENS[idx('network')];
    expect(network.shows!(props())).toBe(false);
    expect(network.shows!(props((p) => (p.status.ethernet = { ...p.status.ethernet, present: true })))).toBe(true);
    expect(network.shows!(props((p) => (p.status.pins = { ...p.status.pins, board: 'wt32-eth01' })))).toBe(true);
  });

  it('hides the site screen while location and time follow SQMeter', () => {
    const site = SCREENS[idx('site')];
    expect(site.shows!(props())).toBe(true);
    const follow = props((p) => {
      p.config.safety.http.url = 'http://sqmeter.local';
      p.config.location = { ...p.config.location, source: 'sqmeter' };
      p.config.sqmeterSync.time.follow = true;
    });
    expect(site.shows!(follow)).toBe(false);
    // The time alone isn't enough.
    const timeOnly = props((p) => {
      p.config.safety.http.url = 'http://sqmeter.local';
      p.config.location = { ...p.config.location, source: 'manual' };
      p.config.sqmeterSync.time.follow = true;
    });
    expect(site.shows!(timeOnly)).toBe(true);
  });

  it('skips N.I.N.A. with the test box park bypass', () => {
    const nina = SCREENS[idx('nina')];
    expect(nina.shows!(props((p) => (p.config.testBox = { ...p.config.testBox, park: false })))).toBe(true);
    expect(nina.shows!(props((p) => (p.config.testBox = { ...p.config.testBox, park: true })))).toBe(false);
    const bypass = props((p) => (p.config.testBox = { ...p.config.testBox, park: true }));
    expect(SCREENS[nextScreen(idx('parkSwitches'), bypass)].id).toBe('direction');
  });

  it('never shows the motion screen in the flow', () => {
    const p = props();
    expect(SCREENS[idx('motion')].shows!(p)).toBe(false);
    expect(SCREENS[nextScreen(idx('calibrate'), p)].id).toBe('alerts');
    expect(SCREENS[previousScreen(idx('alerts'), p)].id).toBe('calibrate');
  });

  it('never makes the SQMeter screen a choice or holds Continue', () => {
    const sqmeter = SCREENS[idx('sqmeter')];
    expect(sqmeter.choice).toBeUndefined();
    expect(sqmeter.answered).toBeUndefined();
    expect(sqmeter.shows).toBeUndefined();
  });
});
