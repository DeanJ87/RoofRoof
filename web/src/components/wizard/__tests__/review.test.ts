import { describe, expect, it } from 'vitest';
import type { Config, RoofInputs, SystemStatus } from '../../../types';
import { mockConfig, mockInputs, mockStatus } from '../../../mocks/data';
import { reviewChecks, type ReviewInput } from '../review';

// Spec 015 FR-010: one check per essential, each state reachable, and a
// missing reading never passes.

const base = (): ReviewInput => ({
  config: structuredClone(mockConfig),
  status: structuredClone(mockStatus),
  inputs: structuredClone(mockInputs),
  hallNotFitted: false,
});

const stateOf = (input: ReviewInput, id: string) => reviewChecks(input).find((check) => check.id === id)!.state;
const given = (change: (input: ReviewInput) => void) => {
  const input = base();
  change(input);
  return input;
};
const roof = (input: ReviewInput) => input.status!.roof!;
const cfg = (input: ReviewInput) => input.config as Required<Config>;

describe('reviewChecks', () => {
  it('lists every essential once, each with its page', () => {
    const ids = reviewChecks(base()).map((check) => check.id);
    expect(ids).toEqual([
      'password',
      'site',
      'clock',
      'supply',
      'parkSwitches',
      'rainContact',
      'hall',
      'motion',
      'weather',
      'nina',
      'alpaca',
      'alerts',
      'daylight',
    ]);
    reviewChecks(base()).forEach((check) => {
      expect(check.label).not.toBe('');
      expect(check.text).not.toBe('');
    });
  });

  it('password: a problem without one', () => {
    expect(
      stateOf(
        given((i) => (i.config.auth = { enabled: true, username: 'admin', password: '********' })),
        'password',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => (i.config.auth = { enabled: false, username: 'admin', password: '' })),
        'password',
      ),
    ).toBe('bad');
    expect(
      stateOf(
        given((i) => (i.config.auth = { enabled: true, username: 'admin', password: '' })),
        'password',
      ),
    ).toBe('bad');
  });

  it('site: needed by daylight', () => {
    expect(
      stateOf(
        given((i) => (cfg(i).location.set = true)),
        'site',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => ((cfg(i).location.set = false), (cfg(i).daylight.mode = 'off'))),
        'site',
      ),
    ).toBe('warn');
    expect(
      stateOf(
        given((i) => ((cfg(i).location.set = false), (cfg(i).daylight.mode = 'close'))),
        'site',
      ),
    ).toBe('bad');
  });

  it('clock: set, not set, or unknown without a status', () => {
    expect(stateOf(base(), 'clock')).toBe('ok');
    expect(
      stateOf(
        given((i) => (i.status!.time.epoch = 0)),
        'clock',
      ),
    ).toBe('warn');
    expect(
      stateOf(
        given((i) => (i.status = null)),
        'clock',
      ),
    ).toBe('unknown');
  });

  it('supply: unreadable and low are problems', () => {
    expect(
      stateOf(
        given((i) => ((roof(i).supplyVolts = 12.6), (roof(i).supplyLow = false))),
        'supply',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => ((roof(i).supplyVolts = 10.2), (roof(i).supplyLow = true))),
        'supply',
      ),
    ).toBe('bad');
    expect(
      stateOf(
        given((i) => (roof(i).supplyVolts = null)),
        'supply',
      ),
    ).toBe('bad');
    expect(
      stateOf(
        given((i) => (i.status = { ...i.status!, roof: undefined } as SystemStatus)),
        'supply',
      ),
    ).toBe('unknown');
  });

  it('park switches: none fitted is attention, an unreadable one a problem', () => {
    expect(
      stateOf(
        given((i) => (cfg(i).park.switches = 'none')),
        'parkSwitches',
      ),
    ).toBe('warn');
    const two = (s1: 'closed' | 'unreadable') =>
      given((i) => {
        cfg(i).park.switches = 'two';
        roof(i).parkSwitches = { fitted: 2, switch1: s1, switch2: 'open' };
      });
    expect(stateOf(two('closed'), 'parkSwitches')).toBe('ok');
    expect(stateOf(two('unreadable'), 'parkSwitches')).toBe('bad');
  });

  it('rain contact: an unreadable fitted contact is a problem', () => {
    expect(
      stateOf(
        given((i) => (cfg(i).safety.rainContact = false)),
        'rainContact',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => ((cfg(i).safety.rainContact = true), (roof(i).rainContact = 'dry'))),
        'rainContact',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => ((cfg(i).safety.rainContact = true), (roof(i).rainContact = 'unreadable'))),
        'rainContact',
      ),
    ).toBe('bad');
  });

  it('Hall sensor: not fitted and at a rail are problems; between the magnets needs a look', () => {
    expect(
      stateOf(
        given((i) => (roof(i).hall = { level: 'low', counts: 900 })),
        'hall',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => (roof(i).hall = { level: 'between', counts: 2000 })),
        'hall',
      ),
    ).toBe('warn');
    expect(
      stateOf(
        given((i) => (roof(i).hall = { level: 'rail', counts: 4095 })),
        'hall',
      ),
    ).toBe('bad');
    expect(
      stateOf(
        given((i) => ((i.hallNotFitted = true), (roof(i).hall = { level: 'low', counts: 900 }))),
        'hall',
      ),
    ).toBe('bad');
  });

  it('motion: the factory travel and speed still need measuring', () => {
    expect(
      stateOf(
        given((i) => ((cfg(i).roof.travelMm = 1850), (cfg(i).roof.speedMmPerSecond = 38))),
        'motion',
      ),
    ).toBe('warn');
    expect(
      stateOf(
        given((i) => ((cfg(i).roof.travelMm = 2010), (cfg(i).roof.speedMmPerSecond = 41.5))),
        'motion',
      ),
    ).toBe('ok');
  });

  it('weather: no source is a problem; unsafe needs a look; no reading is unknown', () => {
    const noSource = given((i) => ((cfg(i).safety.http.enabled = false), (cfg(i).safety.mqtt.enabled = false)));
    expect(stateOf(noSource, 'weather')).toBe('bad');
    const on = (verdict: RoofInputs['weather']['verdict'] | null) =>
      given((i) => {
        cfg(i).safety.http.enabled = true;
        if (verdict) i.inputs!.weather.verdict = verdict;
        else i.inputs = null;
      });
    expect(stateOf(on('safe'), 'weather')).toBe('ok');
    expect(stateOf(on('unsafe'), 'weather')).toBe('warn');
    expect(stateOf(on(null), 'weather')).toBe('unknown');
  });

  it('N.I.N.A.: no address is a problem; not answering or no mount needs a look', () => {
    expect(
      stateOf(
        given((i) => (cfg(i).park.nina.url = '')),
        'nina',
      ),
    ).toBe('bad');
    const url = (change: (i: ReviewInput) => void) =>
      given((i) => {
        cfg(i).park.nina.url = 'http://192.168.1.20:1888';
        change(i);
      });
    expect(
      stateOf(
        url(() => undefined),
        'nina',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        url((i) => (i.inputs!.nina.connected = false)),
        'nina',
      ),
    ).toBe('warn');
    expect(
      stateOf(
        url((i) => (i.inputs!.nina = { ...i.inputs!.nina, state: 'unreachable', text: 'connection refused' })),
        'nina',
      ),
    ).toBe('warn');
    expect(
      stateOf(
        url((i) => (i.inputs = null)),
        'nina',
      ),
    ).toBe('unknown');
  });

  it('Alpaca: off needs a look', () => {
    expect(
      stateOf(
        given((i) => (cfg(i).alpaca.enabled = true)),
        'alpaca',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => (cfg(i).alpaca.enabled = false)),
        'alpaca',
      ),
    ).toBe('warn');
  });

  it('alerts: off or without a channel is a problem', () => {
    const alerts = (enabled: boolean, ntfy: boolean) =>
      given((i) => {
        const a = cfg(i).alerts;
        a.enabled = enabled;
        a.pushover.enabled = a.webhook.enabled = a.mqtt.enabled = false;
        a.ntfy.enabled = ntfy;
      });
    expect(stateOf(alerts(true, true), 'alerts')).toBe('ok');
    expect(stateOf(alerts(true, false), 'alerts')).toBe('bad');
    expect(stateOf(alerts(false, true), 'alerts')).toBe('bad');
  });

  it('daylight: off needs a look', () => {
    expect(
      stateOf(
        given((i) => (cfg(i).daylight.mode = 'off')),
        'daylight',
      ),
    ).toBe('warn');
    expect(
      stateOf(
        given((i) => (cfg(i).daylight.mode = 'alert')),
        'daylight',
      ),
    ).toBe('ok');
    expect(
      stateOf(
        given((i) => (cfg(i).daylight.mode = 'close')),
        'daylight',
      ),
    ).toBe('ok');
  });
});
