import { describe, it, expect } from 'vitest';
import { locationQuery, tabForErrorPath, tabFromLocation } from '../components/settings/tabs';
import { toConfigPayload } from '../components/settings/payload';
import { listReasons, restartReasons } from '../components/settings/restart';
import { mockConfig } from '../mocks/data';

describe('tabFromLocation', () => {
  it('opens the requested tab', () => {
    expect(tabFromLocation('?tab=alerts')).toEqual({ tab: 'alerts', anchor: undefined });
  });

  it('maps the Alpaca setup link onto the Network tab', () => {
    expect(tabFromLocation('?section=alpaca')).toEqual({ tab: 'network', anchor: 'alpaca' });
  });

  it('falls back to Device for unknown values', () => {
    expect(tabFromLocation('?tab=bogus').tab).toBe('device');
    expect(tabFromLocation('?tab=safety').tab).toBe('device');
    expect(tabFromLocation('').tab).toBe('device');
  });
});

describe('tabForErrorPath', () => {
  it('assigns validation errors to the tab that shows the field', () => {
    expect(tabForErrorPath('alerts.ntfy.topic')).toBe('alerts');
    expect(tabForErrorPath('alpaca.enabled')).toBe('network');
    expect(tabForErrorPath('mqtt.broker')).toBe('network');
    expect(tabForErrorPath('ntp.server1')).toBe('time');
    expect(tabForErrorPath('deviceName')).toBe('device');
  });
});

describe('locationQuery', () => {
  it('prefers the query in the hash over the page query', () => {
    expect(locationQuery({ search: '?lang=ar', hash: '#/settings?tab=alerts' })).toBe('?tab=alerts');
    expect(locationQuery({ search: '?tab=network', hash: '' })).toBe('?tab=network');
  });
});

describe('restartReasons', () => {
  const base = toConfigPayload(mockConfig);

  it('is empty for settings applied live', () => {
    const next = { ...base, mqtt: { ...base.mqtt, topic: 'x' }, deviceName: 'Roof 2' };
    expect(restartReasons(base, next)).toEqual([]);
  });

  it('lists boot-time settings that changed', () => {
    const next = {
      ...base,
      alpaca: { enabled: !base.alpaca!.enabled },
      wifi: { ...base.wifi, ipv6: !(base.wifi.ipv6 ?? true) },
    };
    expect(restartReasons(base, next)).toEqual(['IPv6', 'Alpaca discovery']);
    expect(listReasons(['WiFi', 'IPv6', 'Alpaca discovery'])).toBe('WiFi, IPv6 and Alpaca discovery');
  });

  // The WiFi manager reads all its settings at boot (SQMeter spec 011 FR-003).
  it.each([
    ['mdns', false],
    ['autoReconnect', false],
    ['reconnectDelayMs', 2000],
    ['maxReconnectDelayMs', 600000],
  ] as const)('needs a restart for wifi.%s', (key, value) => {
    expect(restartReasons(base, { ...base, wifi: { ...base.wifi, [key]: value } })).toEqual(['WiFi']);
  });
});
