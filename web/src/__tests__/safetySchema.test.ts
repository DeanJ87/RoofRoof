import { describe, it, expect } from 'vitest';
import { getConfigValidationErrors } from '../validation/configSchema';
import { mockConfig } from '../mocks/data';
import type { ParkConfig, SafetyConfig } from '../types/config';

// The safety and park settings (spec 004 contracts/settings.md): the same rules
// as the device (test/test_config_safety).

const baseSafety = mockConfig.safety as SafetyConfig;
const basePark = mockConfig.park as ParkConfig;

const withSafety = (safety: Partial<SafetyConfig>) => getConfigValidationErrors({ ...mockConfig, safety: { ...baseSafety, ...safety } });
const withPark = (park: Partial<ParkConfig>) => getConfigValidationErrors({ ...mockConfig, park: { ...basePark, ...park } });

describe('safety and park settings', () => {
  it('accepts the mock settings', () => {
    expect(withSafety({})).toEqual({});
    expect(withPark({})).toEqual({});
  });

  // Spec 020 FR-036, FR-039: SQMeter's address only, http or https, checked whenever it is set.
  it("takes SQMeter's address in any common form, http or https, and refuses another path", () => {
    expect(withSafety({ http: { enabled: true, url: 'https://sqmeter.local/api/safety' } })['safety.http.url']).toBeUndefined();
    expect(withSafety({ http: { enabled: true, url: '192.168.4.144' } })['safety.http.url']).toBeUndefined();
    expect(withSafety({ http: { enabled: true, url: 'http://192.168.4.144/api/sensors' } })['safety.http.url']).toBeUndefined();
    expect(withSafety({ http: { enabled: true, url: '' } })['safety.http.url']).toBe(
      "SQMeter's address isn't set: e.g. 192.168.1.50 or sqmeter.local",
    );
    expect(withSafety({ http: { enabled: true, url: 'http://[fd00::10]/api/safety' } })['safety.http.url']).toBeUndefined();
    expect(withSafety({ http: { enabled: false, url: 'http://sqm/weather' } })['safety.http.url']).toMatch(/address only/);
    expect(withSafety({ http: { enabled: true, url: 'ftp://sqm' } })['safety.http.url']).toBeDefined();
    expect(withSafety({ http: { enabled: false, url: '' } })['safety.http.url']).toBeUndefined();
  });

  it('checks the SQMeter topic only while MQTT is on', () => {
    expect(withSafety({ mqtt: { enabled: true, topic: 'sqmeter/#' } })['safety.mqtt.topic']).toMatch(/letters, numbers/);
    expect(withSafety({ mqtt: { enabled: true, topic: 'obs/sqmeter' } })['safety.mqtt.topic']).toBeUndefined();
    expect(withSafety({ mqtt: { enabled: false, topic: '' } })['safety.mqtt.topic']).toBeUndefined();
  });

  it('allows an empty N.I.N.A. URL but not https', () => {
    expect(withPark({ nina: { url: '', api: 'v2' } })['park.nina.url']).toBeUndefined();
    expect(withPark({ nina: { url: 'https://nina:1888', api: 'v2' } })['park.nina.url']).toBe('N.I.N.A. URL must start with http://');
    expect(withPark({ nina: { url: 'http://', api: 'v2' } })['park.nina.url']).toBeDefined();
  });

  it('accepts only the documented switch choices and API version', () => {
    expect(withPark({ source: 'ninaAndSwitches', switches: 'two' })).toEqual({});
    // Older firmware sends no source: any switch count goes (the device derives the source).
    expect(withPark({ source: undefined, switches: 'two' })).toEqual({});
    expect(withPark({ switches: 'both' as ParkConfig['switches'] })['park.switches']).toBeDefined();
    expect(withPark({ nina: { url: '', api: 'v3' as 'v2' } })['park.nina.api']).toBeDefined();
  });

  // Spec 024 FR-002: the park source's rules, as the device's.
  it('takes N.I.N.A. only with no switch, a switch source with one or two', () => {
    expect(withPark({ source: 'nina', switches: 'none' })).toEqual({});
    expect(withPark({ source: 'nina', switches: 'one' })['park.switches']).toMatch(/N.I.N.A. only uses no park switch/);
    expect(withPark({ source: 'switches', switches: 'two', nina: { url: '', api: 'v2' } })).toEqual({});
    expect(withPark({ source: 'switches', switches: 'none' })['park.switches']).toMatch(/one or two/);
    expect(withPark({ source: 'ninaAndSwitches', switches: 'none' })['park.switches']).toMatch(/one or two/);
    expect(withPark({ source: 'both' as ParkConfig['source'] })['park.source']).toBeDefined();
  });

  it("refuses the test box's park bypass with park switches only", () => {
    const errors = getConfigValidationErrors({
      ...mockConfig,
      park: { ...basePark, source: 'switches', switches: 'one' },
      testBox: { ...mockConfig.testBox!, park: true },
    });
    expect(errors['testBox.park']).toMatch(/nothing to bypass/);
    expect(
      getConfigValidationErrors({
        ...mockConfig,
        park: { ...basePark, source: 'ninaAndSwitches', switches: 'one' },
        testBox: { ...mockConfig.testBox!, park: true },
      })['testBox.park'],
    ).toBeUndefined();
  });
});

// Spec 024: a secret to take from SQMeter at the save passes the page's checks (the device decides).
describe('the "(from SQMeter)" placeholder', () => {
  it('is accepted as the MQTT password, the ntfy token and the Pushover keys (whose format is otherwise 30 characters)', () => {
    const alerts = mockConfig.alerts!;
    const errors = getConfigValidationErrors({
      ...mockConfig,
      mqtt: { ...mockConfig.mqtt, password: '(from SQMeter)' },
      alerts: {
        ...alerts,
        ntfy: { ...alerts.ntfy, token: '(from SQMeter)' },
        pushover: { ...alerts.pushover, enabled: true, userKey: '(from SQMeter)', appToken: '(from SQMeter)' },
      },
    });
    expect(errors).toEqual({});
    const short = getConfigValidationErrors({
      ...mockConfig,
      alerts: { ...alerts, pushover: { ...alerts.pushover, enabled: true, userKey: 'abc' } },
    });
    expect(short['alerts.pushover.userKey']).toBeDefined();
  });
});
