import { describe, expect, it } from 'vitest';
import { applyPatch, asShown, baseValuesFor, diffConfig, leafPaths, rebaseEdits } from '../configPatch';

const loaded = {
  deviceName: 'roof',
  wifi: { ssid: 'Old', password: '********', ipv6: true, hostname: 'roofroof' },
  park: { nina: { url: '' }, switches: 1 },
  mqtt: { broker: 'a', port: 1883, username: 'u', password: '********', topic: 'roof' },
  alerts: { ntfy: { server: 'https://ntfy.sh', topic: 't', token: '' } },
};

describe('diffConfig', () => {
  it('sends only the leaves that changed', () => {
    const edited = { ...loaded, wifi: { ...loaded.wifi, ipv6: false } };
    expect(diffConfig(loaded, edited)).toEqual({ wifi: { ipv6: false } });
    expect(diffConfig(loaded, loaded)).toBeUndefined();
  });

  it('sends a section the device lacks whole, and arrays and nulls as values', () => {
    expect(diffConfig(loaded, { ...loaded, setup: { step: 'site' } })).toEqual({ setup: { step: 'site' } });
    expect(diffConfig({ a: [1, 2] }, { a: [1, 3] })).toEqual({ a: [1, 3] });
    expect(diffConfig(loaded, { ...loaded, mqtt: { ...loaded.mqtt, password: null } })).toEqual({ mqtt: { password: null } });
  });

  it('never sends a key the form dropped', () => {
    const { deviceName: _dropped, ...rest } = loaded;
    expect(diffConfig(loaded, rest)).toBeUndefined();
  });
});

describe('baseValuesFor', () => {
  it('carries the loaded value of each changed field, null when missing', () => {
    expect(baseValuesFor(loaded, { wifi: { ipv6: false }, location: { latitude: 1 } })).toEqual({
      wifi: { ipv6: true },
      location: { latitude: null },
    });
  });

  it('carries the credential partners of a changed credential', () => {
    expect(baseValuesFor(loaded, { wifi: { password: 'pw' } })).toEqual({ wifi: { password: '********', ssid: 'Old' } });
    expect(baseValuesFor(loaded, { mqtt: { broker: 'b' } })).toEqual({
      mqtt: { broker: 'a', port: 1883, username: 'u', password: '********' },
    });
    expect(baseValuesFor(loaded, { alerts: { ntfy: { token: 'x' } } })).toEqual({
      alerts: { ntfy: { token: '', server: 'https://ntfy.sh', topic: 't' } },
    });
    // A field beside them isn't one: IPv6 doesn't drag the network along.
    expect(baseValuesFor(loaded, { mqtt: { topic: 'x' } })).toEqual({ mqtt: { topic: 'roof' } });
  });
});

describe('applyPatch and leafPaths', () => {
  it('writes a patch over a copy', () => {
    const out = applyPatch(loaded, { park: { nina: { url: 'http://n' } } });
    expect(out.park).toEqual({ nina: { url: 'http://n' }, switches: 1 });
    expect(loaded.park.nina.url).toBe('');
    expect(leafPaths({ a: { b: 1, c: [1] }, d: null })).toEqual(['a.b', 'a.c', 'd']);
  });
});

describe('rebaseEdits', () => {
  it('keeps the edits to fields that did not change on the device', () => {
    const edited = { ...loaded, deviceName: 'obs', park: { ...loaded.park, nina: { url: 'http://mine' } } };
    const fresh = { ...loaded, park: { ...loaded.park, nina: { url: 'http://theirs' } }, wifi: { ...loaded.wifi, ssid: 'New' } };
    const { config, replaced } = rebaseEdits(fresh, loaded, edited, ['park.nina.url']);
    expect(config.deviceName).toBe('obs');
    expect(config.park.nina.url).toBe('http://theirs');
    expect(config.wifi.ssid).toBe('New');
    expect(replaced).toEqual(['park.nina.url']);
  });

  it('drops a password typed for a network that changed', () => {
    const edited = { ...loaded, wifi: { ...loaded.wifi, password: 'typed' } };
    const fresh = { ...loaded, wifi: { ...loaded.wifi, ssid: 'New' } };
    const { config, replaced } = rebaseEdits(fresh, loaded, edited, ['wifi.ssid']);
    expect(config.wifi).toEqual(fresh.wifi);
    expect(replaced).toEqual(['wifi.password']);
  });
});

describe('spec 021 review fixes', () => {
  it("leaves the wizard's progress out of the base", () => {
    expect(baseValuesFor({ setup: { step: 2 } }, { setup: { step: 3 }, deviceName: 'x' })).toEqual({ deviceName: null });
  });

  it('records a saved secret as the device shows it', () => {
    expect(asShown({ mqtt: { password: 'typed', broker: 'b' }, alerts: { ntfy: { token: null } } }, loaded)).toEqual({
      mqtt: { password: '********', broker: 'b' },
      alerts: { ntfy: { token: '' } },
    });
    // A placeholder kept what the page loaded.
    expect(asShown({ wifi: { password: '' } }, loaded)).toEqual({ wifi: { password: '********' } });
  });

  it('keeps no edit when the device named none or as many as it lists', () => {
    const edited = { ...loaded, deviceName: 'obs', wifi: { ...loaded.wifi, password: 'typed' } };
    for (const names of [[], Array.from({ length: 16 }, (_, i) => `k${i}`)]) {
      const { config, replaced } = rebaseEdits(loaded, loaded, edited, names);
      expect(config).toEqual(loaded);
      expect(replaced.sort()).toEqual(['deviceName', 'wifi.password']);
    }
  });
  it("sends SQMeter's address and user name with a typed SQMeter password", () => {
    const withSqm = { ...loaded, safety: { http: { enabled: true, url: 'http://a', username: 'u', password: '********' } } };
    expect(baseValuesFor(withSqm, { safety: { http: { password: 'x' } } })).toEqual({
      safety: { http: { password: '********', url: 'http://a', username: 'u' } },
    });
  });
});
