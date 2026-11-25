import { describe, expect, it } from 'vitest';
import { LIVE_QUIET_MS, isQuiet, liveIntervalMs, reconnectDelay } from '../liveStream';

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

  it('counts a socket quiet after two and a half rest intervals', () => {
    expect(LIVE_QUIET_MS).toBe(5000);
    expect(isQuiet(1000, 1000 + 4999)).toBe(false);
    expect(isQuiet(1000, 1000 + 5000)).toBe(true);
  });
});
