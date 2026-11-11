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

  it('needs a plain http:// SQMeter URL only while HTTP is on', () => {
    expect(withSafety({ http: { enabled: true, url: 'https://sqmeter.local/api/safety' } })['safety.http.url']).toBe(
      'SQMeter safety URL must start with http://',
    );
    expect(withSafety({ http: { enabled: true, url: '' } })['safety.http.url']).toBeDefined();
    expect(withSafety({ http: { enabled: true, url: 'http://[fd00::10]/api/safety' } })['safety.http.url']).toBeUndefined();
    expect(withSafety({ http: { enabled: false, url: 'https://old' } })['safety.http.url']).toBeUndefined();
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
    expect(withPark({ switches: 'two' })).toEqual({});
    expect(withPark({ switches: 'both' as ParkConfig['switches'] })['park.switches']).toBeDefined();
    expect(withPark({ nina: { url: '', api: 'v3' as 'v2' } })['park.nina.api']).toBeDefined();
  });
});
