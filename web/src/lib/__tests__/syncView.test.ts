import { describe, expect, it } from 'vitest';
import { editBatch, editChanges, follows, locked, needs, syncLine } from '../syncView';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { Config, SyncStatus } from '../../types';

// Spec 020: following SQMeter per group.

const sync = (over: Partial<SyncStatus> = {}): SyncStatus => ({
  ...structuredClone(mockStatus.sync!),
  time: { follow: true, syncedAt: 1763540000, refused: null, needs: [] },
  ...over,
});

describe('syncView', () => {
  it('follows only what the settings say, and nothing on older firmware', () => {
    expect(follows(mockConfig, 'time')).toBe(false);
    expect(follows({ ...mockConfig, sqmeterSync: { ...mockConfig.sqmeterSync!, time: { follow: true, syncedAt: 0 } } }, 'time')).toBe(true);
    const { sqmeterSync: _sync, ...older } = mockConfig;
    expect(follows(older, 'mqtt')).toBe(false);
  });

  it("locks a followed group only with SQMeter's address, and MQTT only with roof commands off", () => {
    const following = structuredClone(mockConfig);
    following.sqmeterSync = {
      time: { follow: true, syncedAt: 0 },
      mqtt: { follow: true, syncedAt: 0 },
      alerts: { follow: true, syncedAt: 0 },
    };
    following.safety!.http.url = '';
    expect(locked(following, 'time')).toBe(false);
    following.safety!.http.url = 'http://sqmeter.local/api/safety';
    expect(locked(following, 'time')).toBe(true);
    following.mqtt.roofCommands = true;
    expect(locked(following, 'mqtt')).toBe(false);
  });

  it('says how the sync is going', () => {
    expect(syncLine(sync({ readAgeSeconds: 180 }), 'time')).toEqual({ text: 'Follows SQMeter, checked 3 min ago', problem: false });
    expect(syncLine(sync({ problem: 'unreachable', readAgeSeconds: 60 }), 'time')).toEqual({
      text: 'Follows SQMeter: SQMeter not answering',
      problem: true,
    });
    expect(syncLine(sync({ problem: 'waiting' }), 'time').text).toMatch(/^Follows SQMeter, synced (19 Nov|Nov 19)$/);
    expect(syncLine(sync({ time: { follow: true, syncedAt: null, refused: null, needs: [] } }), 'time').text).toBe(
      'Follows SQMeter, not synced yet',
    );
    // Switched on in the form but not saved: the device doesn't follow yet.
    expect(syncLine(sync(), 'mqtt').text).toBe('Follows SQMeter once saved');
    expect(syncLine(undefined, 'time').text).toBe('Follows SQMeter');
  });

  it('knows which secrets the device asks for', () => {
    const s = sync({ alerts: { follow: true, syncedAt: 1, refused: null, needs: ['ntfyToken'] } });
    expect(needs(s, 'ntfyToken')).toBe(true);
    expect(needs(s, 'mqttPassword')).toBe(false);
    expect(needs(undefined, 'pushoverKeys')).toBe(false);
  });
});

// Spec 026: typing in a followed group sets it here; an asked-for secret only fills the gap.
describe('editChanges', () => {
  const following = (): Config => {
    const c = structuredClone(mockConfig);
    c.sqmeterSync = { time: { follow: true, syncedAt: 1 }, mqtt: { follow: true, syncedAt: 1 }, alerts: { follow: true, syncedAt: 1 } };
    return c;
  };
  it('a field of a followed group unfollows that group only', () => {
    expect(editChanges(following(), ['mqtt', 'username'], 'me')).toEqual([
      [['mqtt', 'username'], 'me'],
      [['sqmeterSync', 'mqtt', 'follow'], false],
    ]);
    expect(editChanges(following(), ['safety', 'mqtt', 'topic'], 'obs')[1]).toEqual([['sqmeterSync', 'mqtt', 'follow'], false]);
    expect(editChanges(following(), ['ntp', 'timezone'], 'UTC0')[1]).toEqual([['sqmeterSync', 'time', 'follow'], false]);
    expect(editChanges(following(), ['alerts', 'pushover', 'userKey'], 'k')[1]).toEqual([['sqmeterSync', 'alerts', 'follow'], false]);
  });
  it('a field outside the groups, or a group set here, is just the edit', () => {
    expect(editChanges(following(), ['mqtt', 'topic'], 'roof')).toEqual([[['mqtt', 'topic'], 'roof']]);
    const local = following();
    local.sqmeterSync!.mqtt.follow = false;
    expect(editChanges(local, ['mqtt', 'username'], 'me')).toHaveLength(1);
  });
  it('a secret the device asks for keeps the group following', () => {
    const sync = { ...mockStatus.sync!, mqtt: { follow: true, syncedAt: 1, refused: null, needs: ['mqttPassword' as const] } };
    expect(editChanges(following(), ['mqtt', 'password'], 'pw', sync)).toHaveLength(1);
    expect(editChanges(following(), ['mqtt', 'password'], 'pw')).toHaveLength(2);
  });
});

// Spec 026: a measured roof value typed over is no longer measured, also several at once; a Pushover sound isn't SQMeter's.
describe('editChanges and editBatch: measured values and the Pushover sound', () => {
  const measured = (bits: number): Config => {
    const c = structuredClone(mockConfig);
    c.roof = { ...c.roof!, measured: bits };
    c.sqmeterSync = { time: { follow: true, syncedAt: 1 }, mqtt: { follow: true, syncedAt: 1 }, alerts: { follow: true, syncedAt: 1 } };
    return c;
  };
  it('clears the bit of the value typed', () => {
    expect(editChanges(measured(31), ['roof', 'travelMm'], 2000)).toEqual([
      [['roof', 'travelMm'], 2000],
      [['roof', 'measured'], 30],
    ]);
  });
  it('combines the bits of several edits', () => {
    const changes = editBatch(measured(31), [
      [['roof', 'travelMm'], 2000],
      [['roof', 'speedMmPerSecond'], 20],
    ]);
    expect(changes.filter(([path]) => path.join('.') === 'roof.measured')).toEqual([[['roof', 'measured'], 28]]);
  });
  it('a Pushover sound keeps the alerts group following', () => {
    expect(editChanges(measured(0), ['alerts', 'pushover', 'sound'], 'siren')).toHaveLength(1);
  });
});
