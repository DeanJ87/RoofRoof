import { describe, it, expect } from 'vitest';
import {
  configSchema,
  authConfigSchema,
  alpacaConfigSchema,
  getConfigValidationErrors,
  getConfigValidationMessage,
  hasConfigValidationErrors,
} from '../validation/configSchema';
import { alertsConfigSchema } from '../validation/alertsSchema';
import { mockConfig } from '../mocks/data';
import type { Config } from '../types';

// Minimal valid config satisfying all schema rules
const validBase: Config = {
  deviceName: 'test-roof',
  wifi: {
    ssid: 'TestNet',
    password: '',
    hostname: 'roof-test',
    autoReconnect: true,
    reconnectDelayMs: 1000,
    maxReconnectDelayMs: 300000,
  },
  mqtt: {
    enabled: false,
    broker: '',
    port: 1883,
    username: '',
    password: '',
    topic: 'roof/test',
    publishIntervalMs: 5000,
  },
  ota: { enabled: false, password: '' },
  auth: { enabled: false, username: 'admin', password: '' },
  ntp: {
    enabled: true,
    server1: 'pool.ntp.org',
    server2: 'time.nist.gov',
    timezone: 'UTC0',
    syncIntervalMs: 600000,
  },
};

describe('configSchema', () => {
  it('accepts a valid config with auth disabled', () => {
    expect(configSchema.safeParse(validBase).success).toBe(true);
  });

  it('accepts auth enabled with credentials', () => {
    const cfg = { ...validBase, auth: { enabled: true, username: 'admin', password: 's3cr3t' } };
    expect(configSchema.safeParse(cfg).success).toBe(true);
  });

  it('rejects auth enabled with empty password', () => {
    const cfg = { ...validBase, auth: { enabled: true, username: 'admin', password: '' } };
    const result = configSchema.safeParse(cfg);
    expect(result.success).toBe(false);
    if (!result.success) {
      const paths = result.error.issues.map((i) => i.path.join('.'));
      expect(paths).toContain('auth.password');
    }
  });

  // D-34: the same rule as the device - a password is required, a username isn't.
  it("rejects auth enabled without a password, with the device's message", () => {
    const cfg = { ...validBase, auth: { enabled: true, username: '', password: '' } };
    const result = configSchema.safeParse(cfg);
    expect(result.success).toBe(false);
    if (!result.success) {
      expect(result.error.issues.map((i) => i.message)).toContain('HTTP auth password is required when auth is enabled');
    }
  });

  // D-33: the device's own message, shown before saving (FR-010).
  it("refuses MQTT without a broker with the device's message", () => {
    const mqtt = { ...validBase, mqtt: { ...validBase.mqtt, enabled: true, broker: '', topic: 'roofroof' } };
    const mqttResult = configSchema.safeParse(mqtt);
    expect(mqttResult.success).toBe(false);
    if (!mqttResult.success)
      expect(mqttResult.error.issues.map((i) => i.message)).toContain('MQTT broker and topic are required when MQTT is enabled');
  });

  it('rejects empty deviceName', () => {
    const cfg = { ...validBase, deviceName: '' };
    expect(configSchema.safeParse(cfg).success).toBe(false);
  });

  it('rejects MQTT enabled with empty broker', () => {
    const cfg = { ...validBase, mqtt: { ...validBase.mqtt, enabled: true, broker: '' } };
    expect(configSchema.safeParse(cfg).success).toBe(false);
  });

  // D-32: a dependency (kept, shown as not in effect), not a rejected save.
  it('accepts OTA enabled without a password', () => {
    const cfg = { ...validBase, ota: { enabled: true, password: '' } };
    expect(configSchema.safeParse(cfg).success).toBe(true);
  });

});

describe('frontend settings validation', () => {
  const setPath = (source: Config, path: string, value: unknown): Config => {
    const candidate = structuredClone(source) as Config;
    const parts = path.split('.');
    let target: Record<string, unknown> = candidate as unknown as Record<string, unknown>;

    for (const part of parts.slice(0, -1)) {
      target = target[part] as Record<string, unknown>;
    }
    target[parts[parts.length - 1]] = value;
    return candidate;
  };

  const validSettings: Array<[path: string, value: unknown]> = [
    ['deviceName', 'Back Garden Roof'],
    ['wifi.ssid', 'Observatory'],
    ['wifi.password', 'wifi-secret'],
    ['wifi.hostname', 'roofroof-garden'],
    ['wifi.autoReconnect', false],
    ['ota.enabled', false],
    ['ota.password', ''],
    ['auth.enabled', false],
    ['auth.username', 'operator'],
    ['auth.password', ''],
    ['ntp.enabled', true],
    ['ntp.timezone', 'GMT0BST,M3.5.0/1,M10.5.0'],
    ['ntp.server1', 'time.cloudflare.com'],
    ['ntp.server2', ''],
    ['ntp.syncIntervalMs', 600000],
    ['mqtt.enabled', true],
    ['mqtt.broker', 'mqtt.example.com'],
    ['mqtt.port', 8883],
    ['mqtt.username', 'roofroof'],
    ['mqtt.password', 'mqtt-secret'],
    ['mqtt.topic', 'observatory/roof'],
    ['mqtt.publishIntervalMs', 1000],
    ['alpaca.enabled', false],
  ];

  it.each(validSettings)('accepts the frontend setting %s', (path, value) => {
    const result = configSchema.safeParse(setPath(mockConfig, path, value));
    expect(result.success, result.success ? undefined : JSON.stringify(result.error.issues)).toBe(true);
  });

  it('allows save when a valid configuration has zero validation errors', () => {
    const errors = getConfigValidationErrors(mockConfig);
    expect(errors).toEqual({});
    expect(hasConfigValidationErrors(errors)).toBe(false);
  });

  it('blocks save and identifies the field when validation fails', () => {
    const errors = getConfigValidationErrors(setPath(mockConfig, 'deviceName', ''));
    expect(errors.deviceName).toBe('Device name is required');
    expect(hasConfigValidationErrors(errors)).toBe(true);
  });

  it('sums up a single error in one line', () => {
    const errors = getConfigValidationErrors(setPath(mockConfig, 'deviceName', ''));
    expect(getConfigValidationMessage(errors)).toBe('Please fix 1 validation error: Device name is required');
  });

  it('never sends a blank WiFi network (spec 011 FR-009)', () => {
    for (const ssid of ['', '   ']) {
      const errors = getConfigValidationErrors(setPath(mockConfig, 'wifi.ssid', ssid));
      expect(errors['wifi.ssid']).toBe('WiFi SSID is required');
      expect(hasConfigValidationErrors(errors)).toBe(true);
    }
  });

});

describe('authConfigSchema', () => {
  it('passes when disabled regardless of credentials', () => {
    expect(authConfigSchema.safeParse({ enabled: false, username: '', password: '' }).success).toBe(true);
  });

  it('passes when enabled with both username and password', () => {
    expect(authConfigSchema.safeParse({ enabled: true, username: 'admin', password: 'hunter2' }).success).toBe(true);
  });

  it('fails when enabled with empty password', () => {
    expect(authConfigSchema.safeParse({ enabled: true, username: 'admin', password: '' }).success).toBe(false);
  });

  it('accepts an empty username, like the device (D-34)', () => {
    expect(authConfigSchema.safeParse({ enabled: true, username: '', password: 'hunter2' }).success).toBe(true);
  });
});

describe('alpacaConfigSchema', () => {
  const validAlpaca = { enabled: false };

  it('passes with valid defaults', () => {
    expect(alpacaConfigSchema.safeParse(validAlpaca).success).toBe(true);
  });

  it('is accepted as an optional field on the full config schema', () => {
    expect(configSchema.safeParse({ ...validBase, alpaca: validAlpaca }).success).toBe(true);
  });

  it('full config schema still passes when alpaca is omitted', () => {
    expect(configSchema.safeParse(validBase).success).toBe(true);
  });
});

describe('mockConfig', () => {
  it('has an auth field', () => {
    expect(mockConfig.auth).toBeDefined();
  });

  it('has auth.enabled = false by default', () => {
    expect(mockConfig.auth.enabled).toBe(false);
  });

  it('has a non-empty auth.username', () => {
    expect(mockConfig.auth.username.length).toBeGreaterThan(0);
  });

  it('has an empty auth.password (not a real credential)', () => {
    expect(mockConfig.auth.password).toBe('');
  });

  it('satisfies configSchema', () => {
    expect(configSchema.safeParse(mockConfig).success).toBe(true);
  });

  it('does not expose a real password value in wifi mock', () => {
    // wifi.password should be empty in the demo config
    expect(mockConfig.wifi.password).toBe('');
  });

  it('does not expose a real password value in mqtt mock', () => {
    expect(mockConfig.mqtt.password).toBe('');
  });
});

describe('alertsConfigSchema', () => {
  const validAlerts = {
    enabled: true,
    events: { client_lost: { level: 3, sound: '' }, client_back: { level: 1, sound: '' } },
    sendMode: 'any',
    clientSilentSeconds: 120,
    cooldownSeconds: 300,
    pushover: { enabled: false, userKey: '', appToken: '', sound: '' },
    ntfy: { enabled: false, server: 'https://ntfy.sh', topic: '', token: '' },
    webhook: { enabled: false, url: '', authHeader: '', insecureTls: false },
    mqtt: { enabled: false },
  };

  it('passes with valid defaults', () => {
    expect(alertsConfigSchema.safeParse(validAlerts).success).toBe(true);
  });

  it('rejects out-of-range levels and odd sound names', () => {
    const withEvent = (level: number, sound: string) =>
      alertsConfigSchema.safeParse({ ...validAlerts, events: { client_lost: { level, sound } } }).success;
    expect(withEvent(5, '')).toBe(false);
    expect(withEvent(2, 'not a sound!')).toBe(false);
    expect(withEvent(4, 'persistent')).toBe(true);
  });

  it('requires 30-character Pushover keys (or the stored mask)', () => {
    const key = 'a'.repeat(30);
    const withKeys = (userKey: string, appToken: string) =>
      alertsConfigSchema.safeParse({ ...validAlerts, pushover: { ...validAlerts.pushover, enabled: true, userKey, appToken } }).success;
    expect(withKeys(key, key)).toBe(true);
    expect(withKeys('********', '********')).toBe(true);
    expect(withKeys(`${key} `, key)).toBe(true); // trailing space from a paste is trimmed
    expect(withKeys('me@example.com', key)).toBe(false);
    expect(withKeys(key, 'short')).toBe(false);
  });

  it('requires Pushover credentials when Pushover is enabled', () => {
    const result = alertsConfigSchema.safeParse({ ...validAlerts, pushover: { ...validAlerts.pushover, enabled: true } });
    expect(result.success).toBe(false);
  });

  it('requires an ntfy topic and http(s) server', () => {
    expect(alertsConfigSchema.safeParse({ ...validAlerts, ntfy: { ...validAlerts.ntfy, enabled: true } }).success).toBe(false);
    expect(
      alertsConfigSchema.safeParse({ ...validAlerts, ntfy: { ...validAlerts.ntfy, enabled: true, topic: 'x', server: 'ntfy.sh' } }).success,
    ).toBe(false);
    expect(alertsConfigSchema.safeParse({ ...validAlerts, ntfy: { ...validAlerts.ntfy, enabled: true, topic: 'x' } }).success).toBe(true);
  });

  it('requires an http(s) webhook URL', () => {
    expect(
      alertsConfigSchema.safeParse({ ...validAlerts, webhook: { ...validAlerts.webhook, enabled: true, url: 'ftp://x' } }).success,
    ).toBe(false);
  });

  it('keeps the silent time within 30 s - 60 min', () => {
    expect(alertsConfigSchema.safeParse({ ...validAlerts, clientSilentSeconds: 29 }).success).toBe(false);
    expect(alertsConfigSchema.safeParse({ ...validAlerts, clientSilentSeconds: 3601 }).success).toBe(false);
  });

  it('caps the cooldown at 24 hours', () => {
    expect(alertsConfigSchema.safeParse({ ...validAlerts, cooldownSeconds: 86401 }).success).toBe(false);
  });
});
