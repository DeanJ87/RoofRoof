import { describe, expect, it } from 'vitest';
import { LIVE_QUIET_MS, TOO_MANY_PAGES, isHeartbeat, isQuiet, liveIntervalMs, reconnectDelay } from '../liveStream';

describe('live stream schedule (spec 028)', () => {
  it('matches the device: 2 s at rest, 500 ms while the roof moves', () => {
    expect(liveIntervalMs(false)).toBe(2000);
    expect(liveIntervalMs(true)).toBe(500);
  });

  it('retries after 1 s, doubling to 30 s, with up to 1 s of jitter', () => {
    expect(reconnectDelay(0, 0)).toBe(1000);
    expect(reconnectDelay(1, 0)).toBe(2000);
    expect(reconnectDelay(4, 0)).toBe(16000);
    expect(reconnectDelay(5, 0)).toBe(30000);
    expect(reconnectDelay(20, 0.999)).toBeLessThan(31000);
  });

  it('counts a socket quiet after five rest intervals (spec 037)', () => {
    expect(LIVE_QUIET_MS).toBe(10000);
    expect(LIVE_QUIET_MS).toBeGreaterThanOrEqual(3 * liveIntervalMs(false) + 2000);
    expect(isQuiet(1000, 1000 + 9999)).toBe(false);
    expect(isQuiet(1000, 1000 + 10000)).toBe(true);
  });

  it('knows the heartbeat and the refusal code', () => {
    expect(isHeartbeat({ heartbeat: true })).toBe(true);
    expect(isHeartbeat({ roof: {} })).toBe(false);
    expect(isHeartbeat(null)).toBe(false);
    expect(TOO_MANY_PAGES).toBe(4429);
  });
});
