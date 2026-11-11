// @vitest-environment node
import { beforeEach, describe, expect, it } from 'vitest';
import createRoofCore from '../core/rr-core.mjs';
import type { Core } from '../coreApi';

// The demo's roof (spec 011): the device core's motion logic, gates, close
// sequence and alerts on the simulated roof, driven with a test clock. Each
// injector gives what the firmware's native tests of the same logic expect.

interface Roof {
  state: string;
  positionMm: number | null;
  fault: { kind: string; cause: string; text: string } | null;
  supplyVolts: number | null;
  simulation: string;
}

const DEFAULTS = {
  safety: { http: { enabled: true, url: 'http://sqmeter.local/api/safety' } },
  park: { nina: { url: 'http://192.168.1.20:1888' } },
  alerts: { enabled: true, ntfy: { enabled: true, topic: 'roofroof-demo' } },
  alpaca: { enabled: true },
};

let core: Core;
let now = 1000;

const tick = (ms: number) => {
  // Ticks of at most a second, as the demo's timer does at 1x.
  for (let left = ms; left > 0; left -= 1000) {
    now += Math.min(1000, left);
    core.tick(now, 1_800_000_000 + Math.floor(now / 1000), '23:00', '2026-11-11');
  }
};
const roof = (): Roof => JSON.parse(core.statusParts()).roof;
const command = (name: string) => JSON.parse(core.roofCommand(name)) as { status: number; body: Record<string, unknown> };
const inject = (change: object) => JSON.parse(core.roofInject(JSON.stringify(change)));
const alertTitles = (): string[] => JSON.parse(core.recentAlerts()).alerts.map((a: { title: string }) => a.title);
const runUntil = (done: () => boolean, maxMs: number) => {
  for (let waited = 0; waited < maxMs; waited += 1000) {
    if (done()) return waited;
    tick(1000);
  }
  return done() ? maxMs : -1;
};

beforeEach(async () => {
  const module = await createRoofCore();
  core = new module.EmulatedDevice(JSON.stringify({ version: 'test', mac: 'a1b2c3d4e5f6' })) as Core;
  core.applyConfig(JSON.stringify(DEFAULTS));
  core.loadConfig(core.getConfig(false));
  now = 1000;
  tick(3000); // boot, the first polls, the Hall settles
});

describe('demo roof', () => {
  it('starts closed and safe, with the simulation named', () => {
    const r = roof();
    expect(r.state).toBe('closed');
    expect(r.positionMm).toBe(0);
    expect(r.simulation).toBe('demo');
    expect(r.supplyVolts).toBeCloseTo(12.6, 1);
    expect(JSON.parse(core.roofPanel()).weather).toBe('safe');
  });

  it('opens and closes in about the travel time (SC-001)', () => {
    expect(command('open').status).toBe(200);
    const openMs = runUntil(() => roof().state === 'open', 120000);
    expect(openMs).toBeGreaterThan((1850 / 38) * 1000 * 0.9);
    expect(openMs).toBeLessThan((1850 / 38) * 1000 * 1.6);
    expect(roof().positionMm).toBe(1850);
    expect(command('close').status).toBe(200);
    expect(runUntil(() => roof().state === 'closed', 120000)).toBeGreaterThan(0);
  });

  it('a stop part-way keeps the estimate', () => {
    command('open');
    tick(10000);
    expect(command('stop').status).toBe(200);
    tick(1000);
    const r = roof();
    expect(r.state).toBe('stopped');
    expect(r.positionMm).toBeGreaterThan(100);
  });

  it('unsafe weather: parks the mount, closes, alerts (US2 1)', () => {
    command('open');
    runUntil(() => roof().state === 'open', 120000);
    inject({ unsafe: true });
    expect(command('open').status).toBe(200); // already open: nothing to refuse
    expect(runUntil(() => roof().state === 'closed', 180000)).toBeGreaterThan(0);
    tick(2000);
    expect(alertTitles().some((t) => /closed/i.test(t))).toBe(true);
    const refused = command('open');
    expect(refused.status).toBe(409);
    expect(refused.body.reason).toBe('weatherNotSafe');
  });

  it('rain closes the roof too, naming rain', () => {
    command('open');
    runUntil(() => roof().state === 'open', 120000);
    inject({ rain: true });
    tick(6000); // the next SQMeter poll (every 5 s), then the verdict
    expect(JSON.parse(core.roofPanel()).weather).toMatch(/rain/i);
    expect(runUntil(() => roof().state === 'closed', 180000)).toBeGreaterThan(0);
  });

  it('park fails: the roof stays open and a critical alert goes out (constitution II)', () => {
    core.applyConfig(JSON.stringify({ park: { timeoutSeconds: 30 } }));
    command('open');
    runUntil(() => roof().state === 'open', 120000);
    inject({ mount: 'unpark', parkFails: true });
    inject({ unsafe: true });
    tick(60000);
    expect(roof().state).toBe('open');
    expect(alertTitles().some((t) => /park/i.test(t))).toBe(true);
  });

  it('a close with the mount unparked is refused', () => {
    command('open');
    runUntil(() => roof().state === 'open', 120000);
    inject({ mount: 'unpark' });
    tick(2000);
    const refused = command('close');
    expect(refused.status).toBe(409);
    expect(refused.body.reason).toBe('mountNotParked');
  });

  it('a jam faults the move; clear fault needs the jam gone', () => {
    inject({ jam: true });
    command('open');
    runUntil(() => roof().state === 'error', 120000);
    expect(roof().fault?.kind).toBe('didNotLeave');
    expect(command('open').body.reason).toBe('faultLatched');
    inject({ jam: false });
    expect(command('clear-fault').status).toBe(200);
    tick(1000);
    expect(roof().fault).toBeNull();
  });

  it('a low supply refuses a move and stops one under way', () => {
    inject({ supplyLow: true });
    tick(1000);
    const refused = command('open');
    expect(refused.status).toBe(409);
    expect(refused.body.error).toMatch(/10\.5 V/);
    inject({ supplyLow: false });
    tick(1000);
    command('open');
    tick(5000);
    inject({ supplyLow: true });
    tick(2000);
    expect(roof().state).toBe('error');
    expect(roof().fault?.kind).toBe('supplyLow');
  });

  it('a reload part-way is a restart: position unknown, fault kept', () => {
    command('open');
    tick(10000);
    command('stop');
    tick(1000);
    const saved = core.saveState();
    const second = core;
    second.loadState(saved);
    tick(3000);
    expect(roof().state).toBe('unknown');
    expect(roof().positionMm).toBeNull();
  });

  it('the Alpaca Dome lists and moves the same roof', () => {
    const devices = JSON.parse(JSON.parse(core.alpaca('GET', '/management/v1/configureddevices', '[]')).body);
    expect(devices.Value[0].DeviceType).toBe('Dome');
    const put = (method: string, params: [string, string][]) =>
      JSON.parse(JSON.parse(core.alpaca('PUT', `/api/v1/dome/0/${method}`, JSON.stringify(params))).body);
    expect(
      put('connected', [
        ['Connected', 'True'],
        ['ClientID', '1'],
        ['ClientTransactionID', '1'],
      ]).ErrorNumber,
    ).toBe(0);
    expect(
      put('openshutter', [
        ['ClientID', '1'],
        ['ClientTransactionID', '2'],
      ]).ErrorNumber,
    ).toBe(0);
    tick(2000);
    const status = JSON.parse(JSON.parse(core.alpaca('GET', '/api/v1/dome/0/shutterstatus', '[]')).body);
    expect(status.Value).toBe(2); // opening
    expect(roof().state).toBe('opening');
  });
});
