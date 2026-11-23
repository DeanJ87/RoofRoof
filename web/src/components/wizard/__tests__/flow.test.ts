// @vitest-environment jsdom
import { afterEach, describe, expect, it } from 'vitest';
import { inPlaceFix, invalidText, startFrom } from '../flow';
import { screenIndex } from '../screens';
import type { DepEntry } from '../../../lib/settingsDeps';

// Spec 025: where the wizard opens (FR-003), what it fixes in place (FR-007)
// and what a refused save says (FR-023).

const query = (q: string) => new URLSearchParams(q);
const entry = (reason: string): DepEntry => ({ id: 'x', setting: 'x', state: 'inactive', reason });

describe('startFrom', () => {
  it('resumes at the saved part in every state, without a start in the address', () => {
    for (const state of ['new', 'offered', 'done'] as const)
      expect(startFrom({ state, step: 9 }, query(''))).toEqual({ index: screenIndex('alerts'), explicit: false });
    expect(startFrom(undefined, query(''))).toEqual({ index: 0, explicit: false });
    expect(startFrom({ state: 'new', step: 99 }, query('')).index).toBe(0); // out of range: the start
  });

  it('an explicit ?screen= or ?step= wins, and says so', () => {
    expect(startFrom({ state: 'new', step: 9 }, query('screen=password'))).toEqual({ index: screenIndex('password'), explicit: true });
    expect(startFrom({ state: 'done', step: 9 }, query('step=1'))).toEqual({ index: 0, explicit: true });
    expect(startFrom({ state: 'new', step: 9 }, query('screen=nope'))).toEqual({ index: screenIndex('alerts'), explicit: false });
  });
});

describe('inPlaceFix', () => {
  it('turns a setting on, opens a wizard screen, or leaves it to Settings', () => {
    expect(inPlaceFix(entry('mqtt-off'))).toEqual({ turnOn: ['mqtt', 'enabled'] });
    expect(inPlaceFix(entry('alerts-off'))).toEqual({ turnOn: ['alerts', 'enabled'] });
    expect(inPlaceFix(entry('alpaca-off'))).toEqual({ turnOn: ['alpaca', 'enabled'] });
    expect(inPlaceFix(entry('location-unknown'))).toEqual({ screen: 'site' });
    expect(inPlaceFix(entry('no-sqmeter-address'))).toEqual({ screen: 'sqmeter' });
    expect(inPlaceFix(entry('mqtt-disconnected'))).toBeNull();
    expect(inPlaceFix({ ...entry(''), reason: undefined, blockedBy: { id: 'y', reason: 'mqtt-off', text: '', fix: '' } })).toEqual({
      turnOn: ['mqtt', 'enabled'],
    });
  });
});

describe('invalidText', () => {
  afterEach(() => document.body.replaceChildren());

  it('names each field by its label on the screen, or by the error for one elsewhere', () => {
    document.body.innerHTML =
      '<div class="field"><label class="field-label"><span>User key</span></label><input data-field="alerts.pushover.userKey"></div>';
    expect(invalidText({ 'alerts.pushover.userKey': 'bad key', 'roof.travelMm': 'Travel must be 100 mm or more' })).toBe(
      'Check User key and Travel must be 100 mm or more.',
    );
  });
});
