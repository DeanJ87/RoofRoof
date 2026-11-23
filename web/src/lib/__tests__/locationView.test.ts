import { describe, expect, it } from 'vitest';
import { locationLine } from '../locationView';
import type { LocationStatus } from '../../types';

// The location's source and age (spec 020 FR-019, FR-020).

const at = (over: Partial<LocationStatus>): LocationStatus => ({
  source: 'sqmeter',
  set: true,
  gps: true,
  savedAt: null,
  readAgeSeconds: null,
  problem: null,
  ...over,
});
const text = (over: Partial<LocationStatus>) => locationLine(at(over))?.text;

describe('locationLine', () => {
  it('says where the location came from and how old it is', () => {
    expect(text({ readAgeSeconds: 180 })).toBe("Location from SQMeter's GPS, read 3 min ago");
    expect(text({ gps: false, readAgeSeconds: 30 })).toBe('Location from SQMeter, read 30 s ago');
    expect(text({ gps: false, savedAt: Date.UTC(2026, 10, 19, 12) / 1000 })).toMatch(/^Location from SQMeter, saved (19 Nov|Nov 19)$/);
    expect(text({})).toBe("Location from SQMeter's GPS");
    expect(text({ source: 'manual', gps: false })).toBe('Typed-in location');
  });

  it('says nothing for a typed-in location not set', () => {
    expect(locationLine(at({ source: 'manual', set: false }))).toBeNull();
    expect(locationLine(undefined)).toBeNull();
  });

  it('says what went wrong, and that the kept location is used', () => {
    expect(locationLine(at({ problem: 'unreachable', readAgeSeconds: 7200 }))).toEqual({
      text: 'SQMeter not answering: using the location read 2 h 0 min ago',
      problem: true,
    });
    expect(text({ problem: 'unreadable' })).toBe("SQMeter's answer can't be read: using the kept location");
    expect(text({ problem: 'noLocation', set: false })).toBe('SQMeter has no location');
    expect(text({ problem: 'passwordNeeded', set: false })).toBe('SQMeter wants a password');
    expect(text({ problem: 'passwordRefused', readAgeSeconds: 60 })).toBe(
      'SQMeter refused the password: using the location read 1 min ago',
    );
    expect(text({ problem: 'noAddress', set: false })).toBe("SQMeter's address isn't set");
    expect(text({ problem: 'waiting', set: false })).toBe("Waiting for SQMeter's location");
    expect(text({ problem: 'waiting', readAgeSeconds: 60 })).toBe("Location from SQMeter's GPS, read 1 min ago");
  });
});
