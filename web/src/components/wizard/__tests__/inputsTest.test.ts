import { describe, expect, it } from 'vitest';
import type { RoofInputs } from '../../../types';
import { mockConfig, mockInputs } from '../../../mocks/data';
import { testAnswered as answered, type TestPart } from '../inputsTest';
import { meterCheck } from '../HardwarePages';

// Spec 015 FR-009, research R4: a Test's answer is a reading of the saved
// settings, never one from before the save.

const doc = (change: (d: RoofInputs) => void = () => undefined) => {
  const d = structuredClone(mockInputs);
  change(d);
  return d;
};

// The mock config has SQMeter over HTTP on, MQTT off and N.I.N.A. set, as mockInputs reads them.
const config = structuredClone(mockConfig);
const testAnswered = (d: RoofInputs, part: TestPart, before: RoofInputs | null, changed: boolean) =>
  answered(d, part, before, changed, config);

describe('testAnswered', () => {
  it('an unchanged part answers with the reading the device holds now', () => {
    expect(testAnswered(doc(), 'weather', doc(), false)).toBe(true);
    expect(testAnswered(doc(), 'nina', null, false)).toBe(true);
  });

  it('a waiting source is never an answer', () => {
    expect(
      testAnswered(
        doc((d) => (d.weather.http.state = 'waiting')),
        'weather',
        doc(),
        false,
      ),
    ).toBe(false);
    expect(
      testAnswered(
        doc((d) => (d.nina.state = 'waiting')),
        'nina',
        doc(),
        false,
      ),
    ).toBe(false);
  });

  it('a changed part waits for a new generation', () => {
    const before = doc();
    expect(testAnswered(doc(), 'weather', before, true)).toBe(false);
    expect(
      testAnswered(
        doc((d) => (d.weather.http.generation += 1)),
        'weather',
        before,
        true,
      ),
    ).toBe(true);
    expect(
      testAnswered(
        doc((d) => (d.weather.mqtt.generation += 1)),
        'weather',
        before,
        true,
      ),
    ).toBe(true);
    expect(testAnswered(doc(), 'nina', before, true)).toBe(false);
    expect(
      testAnswered(
        doc((d) => (d.nina.generation += 1)),
        'nina',
        before,
        true,
      ),
    ).toBe(true);
    // A new generation still waiting for its first reply isn't the answer yet.
    expect(
      testAnswered(
        doc((d) => ((d.nina.generation += 1), (d.nina.state = 'waiting'))),
        'nina',
        before,
        true,
      ),
    ).toBe(false);
  });

  it('after a change, an unchanged source still waiting does not hold the answer back', () => {
    const before = doc((d) => (d.weather.mqtt = { ...d.weather.mqtt, enabled: true, state: 'waiting' }));
    const mqttOn = { ...config, safety: { ...config.safety!, mqtt: { ...config.safety!.mqtt, enabled: true } } };
    const after = doc((d) => ((d.weather.http.generation += 1), (d.weather.mqtt = { ...d.weather.mqtt, enabled: true, state: 'waiting' })));
    expect(answered(after, 'weather', before, true, mqttOn)).toBe(true);
    // The changed source itself still waiting is not an answer.
    const stillWaiting = doc((d) => ((d.weather.http.generation += 1), (d.weather.http.state = 'waiting')));
    expect(testAnswered(stillWaiting, 'weather', doc(), true)).toBe(false);
  });

  it('a changed part with no reading from before the save never answers (it could be the old one)', () => {
    expect(
      testAnswered(
        doc((d) => (d.nina.generation += 1)),
        'nina',
        null,
        true,
      ),
    ).toBe(false);
  });

  it('a reading that does not match the saved settings is not an answer (the device just started)', () => {
    const allOff = doc((d) => ((d.weather.http.enabled = false), (d.weather.http.state = 'off')));
    expect(testAnswered(allOff, 'weather', doc(), false)).toBe(false);
    const notSet = doc((d) => (d.nina.state = 'notConfigured'));
    expect(testAnswered(notSet, 'nina', doc(), false)).toBe(false);
    const cleared = { ...config, park: { ...config.park!, nina: { ...config.park!.nina, url: '' } } };
    expect(
      answered(
        doc((d) => ((d.nina.state = 'notConfigured'), (d.nina.generation += 1))),
        'nina',
        doc(),
        true,
        cleared,
      ),
    ).toBe(true);
  });

  it('a failure of the new settings is an answer', () => {
    const before = doc();
    const failed = doc((d) => ((d.nina.generation += 1), (d.nina.state = 'unreachable')));
    expect(testAnswered(failed, 'nina', before, true)).toBe(true);
  });
});

describe('meterCheck', () => {
  it('agrees within 0.2 V, either way', () => {
    expect(meterCheck(12.6, 12.8)).toBe('agrees');
    expect(meterCheck(12.6, 12.4)).toBe('agrees');
    expect(meterCheck(12.6, 12.81)).toBe('differs');
    expect(meterCheck(12.6, 12.39)).toBe('differs');
  });

  it('says nothing without both readings', () => {
    expect(meterCheck(null, 12.6)).toBe('none');
    expect(meterCheck(12.6, Number.NaN)).toBe('none');
    expect(meterCheck(12.6, 0)).toBe('none');
  });
});
