import { describe, expect, it } from 'vitest';
import { follows, locked, needs, syncLine } from '../syncView';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { SyncStatus } from '../../types';

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
