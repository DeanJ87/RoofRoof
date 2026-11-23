import { describe, expect, it } from 'vitest';
import { mockConfig, mockSqmeterSite } from '../../mocks/data';
import { toConfigPayload } from '../../components/settings/payload';
import { COPIED_SECRET, changesOf, copyNotes, copyRows, locationSource, secretsOutcome, type CopyRowId } from '../sqmeterCopy';
import type { Config, SqmeterSecrets, SqmeterSite } from '../../types';

// Copy site settings from SQMeter (spec 020 FR-014 to FR-018).

const config = (over: (c: Config) => void = () => undefined): Config => {
  const c = toConfigPayload(structuredClone(mockConfig));
  over(c);
  return c;
};
const site = (over: Partial<SqmeterSite> = {}): SqmeterSite => ({ ...structuredClone(mockSqmeterSite), ...over });
const ids = (s: SqmeterSite, c: Config) => copyRows(s, c).map((row) => row.id);
const row = (s: SqmeterSite, c: Config, id: CopyRowId) => copyRows(s, c).find((r) => r.id === id);
const paths = (s: SqmeterSite, c: Config) => copyRows(s, c).flatMap((r) => r.changes.map(([path]) => path.join('.')));

// RoofRoof set up exactly as SQMeter: nothing differs.
const matching = () =>
  config((c) => {
    c.location = { ...c.location!, source: 'sqmeter' };
    c.ntp = { ...c.ntp, timezone: 'GMT0BST,M3.5.0/1,M10.5.0', server1: 'pool.ntp.org', server2: 'time.nist.gov' };
    c.mqtt = { ...c.mqtt, broker: '192.168.1.10', port: 1883, username: 'observatory' };
    c.safety!.mqtt.topic = 'sqmeter';
    c.alerts!.ntfy = { ...c.alerts!.ntfy, server: 'https://ntfy.sh', topic: 'my-observatory' };
  });

describe('copyRows', () => {
  it('lists each setting that differs', () => {
    expect(
      ids(
        mockSqmeterSite,
        config((c) => (c.safety!.mqtt.topic = 'old')),
      ),
    ).toEqual(['location', 'timezone', 'ntp', 'mqttBroker', 'weatherTopic', 'ntfy']);
  });

  it('lists nothing when RoofRoof already matches', () => {
    expect(copyRows(mockSqmeterSite, matching())).toEqual([]);
  });

  it('lists only the setting that differs', () => {
    expect(
      ids(
        mockSqmeterSite,
        config((c) => Object.assign(c, matching(), { ntp: { ...matching().ntp, timezone: 'UTC0' } })),
      ),
    ).toEqual(['timezone']);
  });

  it("never changes RoofRoof's own MQTT topic, an on/off switch, a name or a secret's value", () => {
    const all = paths(
      mockSqmeterSite,
      config((c) => (c.safety!.mqtt.topic = 'old')),
    );
    expect(all).not.toContain('mqtt.topic');
    expect(all.filter((p) => p.endsWith('enabled'))).toEqual([]);
    for (const p of ['deviceName', 'wifi.hostname', 'alerts.pushover.userKey', 'alerts.pushover.appToken']) expect(all).not.toContain(p);
    // A secret is only ever cleared, never given a value.
    for (const r of copyRows(mockSqmeterSite, config()))
      for (const [path, value] of r.changes) if (/password|token/i.test(path.join('.'))) expect(value).toBeNull();
  });

  it("puts SQMeter's base topic into the MQTT weather source's topic", () => {
    expect(
      row(
        mockSqmeterSite,
        config((c) => (c.safety!.mqtt.topic = 'old')),
        'weatherTopic',
      )!.changes,
    ).toEqual([
      [['safety', 'mqtt', 'topic'], 'sqmeter'],
      [['sqmeterSync', 'mqtt', 'follow'], true],
    ]);
  });

  it('clears the MQTT password for a different broker, port or user name, and asks for it', () => {
    for (const change of [{ broker: 'other' }, { port: 8883 }, { username: 'me' }]) {
      const c = matching();
      Object.assign(c.mqtt, change);
      const r = row(mockSqmeterSite, c, 'mqttBroker')!;
      expect(r.changes).toContainEqual([['mqtt', 'password'], null]);
      expect(r.secrets).toEqual(['mqttPassword']);
    }
  });

  it('clears the ntfy token only for a different server; asks for it then or when SQMeter has one', () => {
    const topicOnly = config((c) => (c.alerts!.ntfy = { ...c.alerts!.ntfy, server: 'https://ntfy.sh', topic: 'old' }));
    expect(row(mockSqmeterSite, topicOnly, 'ntfy')!.changes.map(([p]) => p.join('.'))).toEqual([
      'alerts.ntfy.server',
      'alerts.ntfy.topic',
      'sqmeterSync.alerts.follow',
    ]);
    expect(row(mockSqmeterSite, topicOnly, 'ntfy')!.secrets).toEqual([]);
    expect(row(site({ ntfy: { ...mockSqmeterSite.ntfy!, tokenSet: true } }), topicOnly, 'ntfy')!.secrets).toEqual(['ntfyToken']);
    const otherServer = config((c) => (c.alerts!.ntfy = { ...c.alerts!.ntfy, server: 'https://ntfy.example', topic: 'my-observatory' }));
    const r = row(mockSqmeterSite, otherServer, 'ntfy')!;
    expect(r.changes).toContainEqual([['alerts', 'ntfy', 'token'], null]);
    // No token on either side: nothing to enter.
    expect(r.secrets).toEqual([]);
    const withToken = config(
      (c) => (c.alerts!.ntfy = { ...c.alerts!.ntfy, server: 'https://ntfy.example', topic: 'x', token: '********' }),
    );
    expect(row(mockSqmeterSite, withToken, 'ntfy')!.secrets).toEqual(['ntfyToken']);
  });

  it("offers following SQMeter's location while it is typed in and SQMeter has a location section", () => {
    const r = row(mockSqmeterSite, config(), 'location')!;
    expect(r.changes).toContainEqual([['location', 'source'], 'sqmeter']);
    expect(r.theirs).toBe('51.4769 N, 0.0005 W');
    expect(ids(mockSqmeterSite, matching())).not.toContain('location');
    // GPS only: its typed location unset, the row still offered, only the source sent.
    const live = row(site({ location: { set: false, latitude: 0, longitude: 0 } }), config(), 'location')!;
    expect(live.changes).toEqual([[['location', 'source'], 'sqmeter']]);
    expect(live.theirs).toBe('its live location');
    expect(ids(site({ location: null }), config())).not.toContain('location');
  });

  it('leaves out what an older SQMeter lacks, and empty values', () => {
    const old = site({ location: null, time: null, mqtt: null, ntfy: null, pushover: null });
    expect(copyRows(old, config())).toEqual([]);
    expect(copyNotes(old)).toEqual([]);
    const empty = site({
      time: { timezone: '', server1: '', server2: '' },
      mqtt: { broker: '', port: 1883, username: '', topic: '', passwordSet: false },
      ntfy: { server: 'https://ntfy.sh', topic: '', tokenSet: false },
    });
    expect(ids(empty, config())).toEqual(['location']);
  });
});

describe('copyNotes, changesOf, locationSource', () => {
  it("notes SQMeter's Pushover keys, which are never copied", () => {
    expect(copyNotes(mockSqmeterSite)).toEqual(['pushover']);
    expect(copyNotes(site({ pushover: { userKeySet: false, appTokenSet: false } }))).toEqual([]);
  });

  it('applies only the ticked rows', () => {
    const rows = copyRows(mockSqmeterSite, config());
    expect(changesOf(rows, new Set<CopyRowId>(['timezone']))).toEqual([
      [['ntp', 'timezone'], 'GMT0BST,M3.5.0/1,M10.5.0'],
      [['sqmeterSync', 'time', 'follow'], true],
    ]);
    expect(changesOf(rows, new Set())).toEqual([]);
  });

  it('a copied group keeps following SQMeter; the location has its own source; older firmware has no sync', () => {
    const rows = copyRows(
      mockSqmeterSite,
      config((c) => (c.safety!.mqtt.topic = 'old')),
    );
    const follow = (id: CopyRowId) =>
      rows
        .find((r) => r.id === id)!
        .changes.filter(([p]) => p[0] === 'sqmeterSync')
        .map(([p, v]) => [p.join('.'), v]);
    expect(follow('timezone')).toEqual([['sqmeterSync.time.follow', true]]);
    expect(follow('ntp')).toEqual([['sqmeterSync.time.follow', true]]);
    expect(follow('mqttBroker')).toEqual([['sqmeterSync.mqtt.follow', true]]);
    expect(follow('weatherTopic')).toEqual([['sqmeterSync.mqtt.follow', true]]);
    expect(follow('ntfy')).toEqual([['sqmeterSync.alerts.follow', true]]);
    expect(follow('location')).toEqual([]);
    const old = copyRows(
      mockSqmeterSite,
      config((c) => delete c.sqmeterSync),
    );
    expect(old.flatMap((r) => r.changes).filter(([p]) => p[0] === 'sqmeterSync')).toEqual([]);
  });

  it('reads settings from before spec 020: typed in when set, else SQMeter', () => {
    expect(locationSource({ set: true, latitude: 1, longitude: 2 })).toBe('manual');
    expect(locationSource({ set: false, latitude: 0, longitude: 0 })).toBe('sqmeter');
    expect(locationSource({ source: 'sqmeter', set: true, latitude: 1, longitude: 2 })).toBe('sqmeter');
  });

  it('a cleared secret reaches the device as null', () => {
    const payload = toConfigPayload({
      ...config(),
      mqtt: { ...config().mqtt, password: null },
      alerts: { ...config().alerts!, ntfy: { ...config().alerts!.ntfy, token: null } },
    });
    const sent = JSON.parse(JSON.stringify(payload)) as Config;
    expect(sent.mqtt.password).toBeNull();
    expect(sent.alerts!.ntfy.token).toBeNull();
  });
});

// Spec 024: SQMeter's shared secrets (never their values) and its on/off switches.
describe('copyRows: shared secrets and switches', () => {
  const shared = (over: Partial<SqmeterSecrets> = {}, siteOver: Partial<SqmeterSite> = {}): SqmeterSite =>
    site({
      ...siteOver,
      secrets: { problem: null, http: true, mqttPassword: 'received', ntfyToken: 'received', pushover: 'received', ...over },
    });
  const value = (r: { changes: [string[], unknown][] } | undefined, path: string) => r?.changes.find(([p]) => p.join('.') === path)?.[1];

  it("the broker row carries SQMeter's password, received or the same, with nothing to enter", () => {
    for (const state of ['received', 'same'] as const) {
      const r = row(shared({ mqttPassword: state }), config(), 'mqttBroker')!;
      expect(value(r, 'mqtt.password')).toBe(COPIED_SECRET);
      expect(r.secrets).toEqual([]);
      expect(r.asks).toEqual([]);
    }
    // Not received: cleared and asked for, as before.
    expect(value(row(shared({ mqttPassword: 'none' }), config(), 'mqttBroker'), 'mqtt.password')).toBeNull();
  });

  it('offers the MQTT password for the same broker, only when received and different', () => {
    const r = row(shared(), matching(), 'mqttPassword')!;
    expect(r.changes).toEqual([
      [['mqtt', 'password'], COPIED_SECRET],
      [['sqmeterSync', 'mqtt', 'follow'], true],
    ]);
    expect(ids(shared({ mqttPassword: 'same' }), matching())).not.toContain('mqttPassword');
    expect(ids(shared(), config())).not.toContain('mqttPassword'); // another broker: the broker row carries it
    const applied = matching();
    applied.mqtt.password = COPIED_SECRET;
    expect(ids(shared(), applied)).not.toContain('mqttPassword');
  });

  it("turns RoofRoof's MQTT, ntfy and Pushover on when SQMeter's are, never off", () => {
    const on = shared(
      {},
      {
        mqtt: { ...mockSqmeterSite.mqtt!, enabled: true },
        ntfy: { ...mockSqmeterSite.ntfy!, enabled: true },
        pushover: { ...mockSqmeterSite.pushover!, enabled: true },
      },
    );
    const off = matching();
    off.mqtt.enabled = false;
    off.alerts!.pushover.enabled = false;
    expect(row(on, off, 'mqttEnabled')!.changes[0]).toEqual([['mqtt', 'enabled'], true]);
    expect(row(on, off, 'ntfyEnabled')!.changes[0]).toEqual([['alerts', 'ntfy', 'enabled'], true]);
    expect(row(on, off, 'pushoverEnabled')!.changes[0]).toEqual([['alerts', 'pushover', 'enabled'], true]);
    // Already on here, or off at SQMeter: no row, and nothing ever turned off.
    const already = matching();
    expect(ids(on, already)).not.toContain('mqttEnabled');
    for (const r of copyRows(shared(), off)) for (const [p, v] of r.changes) if (p[p.length - 1] === 'enabled') expect(v).toBe(true);
    expect(ids(shared(), off)).not.toContain('mqttEnabled');
    // Pushover on needs keys: received, or RoofRoof's own.
    const noKeys = matching();
    noKeys.alerts!.pushover = { ...noKeys.alerts!.pushover, enabled: false, userKey: '', appToken: '' };
    expect(ids({ ...on, secrets: { ...on.secrets!, pushover: 'none' } }, noKeys)).not.toContain('pushoverEnabled');
    expect(ids(on, noKeys)).toContain('pushoverEnabled');
  });

  it('a switch row brings what the switch needs when RoofRoof lacks it, so no tick combination is refused', () => {
    const on = shared(
      {},
      {
        mqtt: { ...mockSqmeterSite.mqtt!, enabled: true },
        ntfy: { ...mockSqmeterSite.ntfy!, enabled: true },
        pushover: { ...mockSqmeterSite.pushover!, enabled: true },
      },
    );
    const empty = config((c) => {
      c.mqtt = { ...c.mqtt, enabled: false, broker: '', username: '', password: '' };
      c.alerts!.ntfy = { ...c.alerts!.ntfy, enabled: false, topic: '' };
      c.alerts!.pushover = { ...c.alerts!.pushover, enabled: false, userKey: '', appToken: '' };
    });
    const mqtt = row(on, empty, 'mqttEnabled')!;
    expect(value(mqtt, 'mqtt.broker')).toBe('192.168.1.10');
    expect(value(mqtt, 'mqtt.password')).toBe(COPIED_SECRET);
    expect(value(row(on, empty, 'ntfyEnabled'), 'alerts.ntfy.topic')).toBe('my-observatory');
    const pushover = row(on, empty, 'pushoverEnabled')!;
    expect(value(pushover, 'alerts.pushover.userKey')).toBe(COPIED_SECRET);
    expect(value(pushover, 'alerts.pushover.appToken')).toBe(COPIED_SECRET);
    // RoofRoof has its own: only the switch.
    const own = matching();
    own.mqtt.enabled = false;
    own.alerts!.pushover.enabled = false;
    expect(value(row(on, own, 'mqttEnabled'), 'mqtt.broker')).toBeUndefined();
    expect(value(row(on, own, 'ntfyEnabled'), 'alerts.ntfy.topic')).toBeUndefined();
    expect(value(row(on, own, 'pushoverEnabled'), 'alerts.pushover.userKey')).toBeUndefined();
  });

  it("a switch row that carries the broker still asks for a password SQMeter didn't share", () => {
    const notShared = shared({ mqttPassword: 'none' }, { mqtt: { ...mockSqmeterSite.mqtt!, enabled: true, passwordSet: true } });
    const empty = config((c) => (c.mqtt = { ...c.mqtt, enabled: false, broker: '', username: '', password: '' }));
    const r = row(notShared, empty, 'mqttEnabled')!;
    expect(value(r, 'mqtt.broker')).toBe('192.168.1.10');
    expect(value(r, 'mqtt.password')).toBeNull();
    expect(r.asks).toEqual(['mqttPassword']);
    expect(r.secrets).toEqual(['mqttPassword']);
  });

  it('carries the ntfy token with a new server, offers it for the same server, and the Pushover keys', () => {
    const otherServer = config((c) => (c.alerts!.ntfy = { ...c.alerts!.ntfy, server: 'https://ntfy.example', topic: 'x' }));
    const r = row(shared(), otherServer, 'ntfy')!;
    expect(value(r, 'alerts.ntfy.token')).toBe(COPIED_SECRET);
    expect(r.secrets).toEqual([]);
    expect(ids(shared(), otherServer)).not.toContain('ntfyToken');
    expect(value(row(shared(), matching(), 'ntfyToken'), 'alerts.ntfy.token')).toBe(COPIED_SECRET);
    expect(ids(shared({ ntfyToken: 'same' }), matching())).not.toContain('ntfyToken');
    const keys = row(shared(), matching(), 'pushoverKeys')!;
    expect(value(keys, 'alerts.pushover.userKey')).toBe(COPIED_SECRET);
    expect(value(keys, 'alerts.pushover.appToken')).toBe(COPIED_SECRET);
    expect(value(keys, 'sqmeterSync.alerts.follow')).toBe(true);
    expect(ids(shared({ pushover: 'same' }), matching())).not.toContain('pushoverKeys');
  });

  it('notes the Pushover keys only when SQMeter has them and did not share them', () => {
    expect(copyNotes(shared())).toEqual([]);
    expect(copyNotes(shared({ pushover: 'same' }))).toEqual([]);
    expect(copyNotes(shared({ problem: 'sharingOff', pushover: 'none' }))).toEqual(['pushover']);
  });

  it('says what became of the secrets: received (and over http), or why not', () => {
    expect(secretsOutcome(shared())).toEqual({ received: true, http: true, problem: null });
    expect(secretsOutcome(shared({ http: false }))).toEqual({ received: true, http: false, problem: null });
    const problem = (p: SqmeterSecrets['problem']) =>
      secretsOutcome(shared({ problem: p, mqttPassword: 'none', ntfyToken: 'none', pushover: 'none' })).problem;
    for (const p of [
      'notSaved',
      'notSupported',
      'sharingOff',
      'noWebPassword',
      'loginNeeded',
      'loginRefused',
      'tooManyLogins',
      'unreachable',
    ] as const)
      expect(problem(p)).toBe(p);
    expect(problem('waiting')).toBeNull();
    // Nothing set at SQMeter: nothing to say. Older firmware: no secrets part.
    const bare = shared(
      { problem: 'notSupported' },
      {
        mqtt: { ...mockSqmeterSite.mqtt!, passwordSet: false },
        pushover: { userKeySet: false, appTokenSet: false },
      },
    );
    expect(secretsOutcome(bare).problem).toBeNull();
    expect(secretsOutcome({ ...mockSqmeterSite, secrets: undefined })).toEqual({ received: false, http: false, problem: null });
  });
});
