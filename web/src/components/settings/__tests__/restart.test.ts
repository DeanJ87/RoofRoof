import { describe, expect, it } from 'vitest';
import cases from '../../../../../test/fixtures/backup/restart-cases.json';
import { restartCodes, restartReasons } from '../restart';
import { toConfigPayload } from '../payload';
import { mockConfig } from '../../../mocks/data';
import type { Config } from '../../../types';

// The same cases as the device's Backup::restartReasons (test/test_config_backup),
// so a restore restarts exactly when a settings save would ask for one.

const isObject = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value);
const merge = (base: Record<string, unknown>, over: Record<string, unknown>): Record<string, unknown> =>
  Object.fromEntries(
    Object.keys({ ...base, ...over }).map((key) => {
      const a = base[key];
      const b = over[key];
      return [key, isObject(a) && isObject(b) ? merge(a, b) : b === undefined ? a : b];
    }),
  );
const withOverrides = (over: Record<string, unknown>) =>
  merge(toConfigPayload(mockConfig) as unknown as Record<string, unknown>, over) as unknown as Config;

describe('restart reasons (shared with the device)', () => {
  for (const c of cases.cases) {
    it(`restarts for ${c.name}: ${c.reasons.join(', ') || 'none'}`, () => {
      expect(restartCodes(withOverrides(c.before), withOverrides(c.after))).toEqual(c.reasons);
    });
  }
});

// Spec 023: the pins and the network mode are read at boot.
describe('restart reasons for the board and the network mode', () => {
  const base = toConfigPayload(mockConfig);
  it('a pin change needs a restart, named "the pins"', () => {
    const after = { ...base, pins: { ...base.pins!, preset: 'custom' as const, lpwm: 15 } };
    expect(restartCodes(base, after)).toEqual(['pins']);
    expect(restartReasons(base, after)).toEqual(['the pins']);
  });
  it('the network mode restarts with the WiFi settings', () => {
    expect(restartCodes(base, { ...base, network: { mode: 'wifi' } })).toEqual(['wifi']);
  });
  it('unchanged pins and mode need none', () => {
    expect(restartCodes(base, structuredClone(base))).toEqual([]);
  });
});
