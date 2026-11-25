import { describe, expect, it } from 'vitest';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { AlertSchedule, Config, SystemStatus } from '../../types';
import type { EffectiveReport } from '../../lib/settingsDeps';
import { glanceItems, toCheck, type GlanceInput } from '../glance';

// Every visibility rule of the Status card (SQMeter specs 025 FR-005..FR-017, 026 FR-002):
// the imaging app and alerts, plus problems no other card shows (DS-08).

const input = (over: Partial<GlanceInput> = {}): GlanceInput => ({
  status: structuredClone(mockStatus),
  config: withConfig(setUp), // a roof whose setup is finished (spec 028's row aside)
  effective: { facts: {} as EffectiveReport['facts'], settings: [] },
  schedule: { armed: true, mode: 'any', reason: 'none', since: null, sinceAgeMs: null },
  ...over,
});

const ids = (over: Partial<GlanceInput>) =>
  glanceItems(input(over))
    .filter((item) => item.severity === 'note' || item.severity === 'problem')
    .map((item) => item.id);

// Every setup stage done (spec 026 stages; spec 028's Finish setup row stays away).
function setUp(c: Config) {
  c.pins = { ...c.pins!, preset: 'devkit' } as typeof c.pins;
  c.safety!.http.enabled = true;
  c.park!.nina.url = 'http://nina.local:1888';
  c.roof!.measured = 31;
  c.alerts!.enabled = true;
  c.alerts!.ntfy.enabled = true;
}

const withConfig = (change: (config: Config) => void) => {
  const config = structuredClone(mockConfig);
  change(config);
  return config;
};

const withStatus = (change: (status: SystemStatus) => void) => {
  const status = structuredClone(mockStatus);
  change(status);
  return status;
};

describe('glanceItems', () => {
  it('is all good when all is well', () => {
    const status = withStatus((s) => {
      s.alpaca!.clients = { dome: { connected: true, watching: true, silent: false, lastCheckedAgeMs: 2000, clientId: 4021 } };
    });
    const items = glanceItems(input({ status, config: withConfig(setUp) }));
    expect(toCheck(items)).toEqual([]);
    // Tiles: the imaging app per Alpaca device, then Alerts.
    expect(items.map((item) => [item.label, item.state])).toEqual(
      expect.arrayContaining([
        ['Alerts', 'Sending'],
        ['Imaging app', 'Connected'],
      ]),
    );
  });

  it('shows paused alerts with a Resume action (FR-008)', () => {
    const schedule: AlertSchedule = { armed: false, mode: 'any', reason: 'user-ui', since: null, sinceAgeMs: 60_000 };
    const item = glanceItems(input({ schedule, config: withConfig((c) => (c.alerts!.enabled = true)) })).find(
      (i) => i.id === 'alerts-state',
    );
    expect(item).toMatchObject({ severity: 'problem', state: 'Paused', fix: { action: 'resume' } });
    expect(item?.sub).toBeUndefined();
    expect(item?.detail).toMatch(/^Paused by you/);
  });

  it('says alerts are waiting for an imaging app (US1-3)', () => {
    const schedule: AlertSchedule = { armed: false, mode: 'whileConnected', reason: 'waiting-for-client', since: null, sinceAgeMs: null };
    const config = withConfig((c) => {
      c.alerts!.enabled = true;
      c.alerts!.sendMode = 'whileConnected';
    });
    const item = glanceItems(input({ schedule, config })).find((i) => i.id === 'alerts-state');
    expect(item).toMatchObject({ state: 'Waiting', detail: 'Nothing is sent until an imaging app connects.' });
    expect(item?.sub).toBeUndefined();
  });

  it('says alerts are off once', () => {
    expect(ids({ config: withConfig((c) => (c.alerts!.enabled = false)) })).toContain('alerts-state');
  });

  it('says the send mode is not in effect when Alpaca is off (FR-010)', () => {
    const config = withConfig((c) => {
      c.alerts!.enabled = true;
      c.alerts!.sendMode = 'whileConnected';
    });
    const status = withStatus((s) => (s.alpaca!.enabled = false));
    expect(ids({ config, status })).toContain('alerts-mode-not-in-effect');
  });

  it('says no channel can send (US1-6)', () => {
    const config = withConfig((c) => {
      c.alerts!.enabled = true;
      c.alerts!.pushover.enabled = false;
      c.alerts!.ntfy.enabled = false;
      c.alerts!.webhook.enabled = false;
      c.alerts!.mqtt.enabled = true;
    });
    const effective: EffectiveReport = {
      facts: {} as EffectiveReport['facts'],
      settings: [{ id: 'D-01', setting: 'alerts.mqtt.enabled', state: 'inactive', reason: 'mqtt-off', text: 'MQTT is off' }],
    };
    const item = glanceItems(input({ config, effective })).find((i) => i.id === 'no-channel');
    expect(item).toMatchObject({ severity: 'problem', detail: 'MQTT is off' });
    const none = withConfig((c) => {
      c.alerts!.enabled = true;
      (['pushover', 'ntfy', 'webhook', 'mqtt'] as const).forEach((channel) => (c.alerts![channel].enabled = false));
    });
    expect(ids({ config: none })).toContain('no-channel');
  });

  it('shows the imaging app per Alpaca device, none without one; not connected counts only when alerts wait for one (FR-009)', () => {
    const quiet = { connected: false, watching: false, silent: false, lastCheckedAgeMs: null, clientId: null };
    const status = withStatus((s) => {
      s.alpaca!.clients = { dome: { ...quiet } };
    });
    const resting = glanceItems(input({ status })).filter((i) => i.id === 'imaging-app');
    expect(resting).toHaveLength(1);
    expect(resting.every((i) => i.severity === 'idle')).toBe(true);
    expect(toCheck(glanceItems(input({ status })))).toEqual([]);
    const silent = withStatus((s) => {
      s.alpaca!.clients = {
        dome: { ...quiet, connected: true, silent: true, lastCheckedAgeMs: 120_000 },
      };
    });
    expect(glanceItems(input({ status: silent })).find((i) => i.id === 'imaging-app')).toMatchObject({ severity: 'problem' });
    const needed = withConfig((c) => {
      c.alerts!.enabled = true;
      c.alerts!.sendMode = 'whileConnected';
    });
    expect(glanceItems(input({ status, config: needed })).filter((i) => i.id === 'imaging-app' && i.severity === 'note')).toHaveLength(1);
    const off = withStatus((s) => (s.alpaca!.enabled = false));
    expect(glanceItems(input({ status: off })).some((i) => i.id === 'imaging-app')).toBe(false);
    const none = withStatus((s) => {
      s.alpaca!.clients = {};
    });
    expect(glanceItems(input({ status: none })).some((i) => i.id === 'imaging-app')).toBe(false);
  });

  it('leaves settings not in effect to Settings', () => {
    const effective: EffectiveReport = {
      facts: {} as EffectiveReport['facts'],
      settings: [{ id: 'D-37', setting: 'alerts.events.client_lost.level', state: 'inactive', text: 'Alpaca is off' }],
    };
    expect(glanceItems(input({ effective })).some((i) => i.id === 'settings-not-in-effect')).toBe(false);
  });

  it('says the clock is not set (FR-016)', () => {
    expect(ids({ status: withStatus((s) => (s.time.epoch = 0)) })).toContain('clock-location');
    expect(ids({})).not.toContain('clock-location');
  });

  // Spec 028: one row for an unfinished setup, naming the first stage left and linking to it.
  it('says Finish setup with the first stage left, and nothing once every stage is done', () => {
    const weatherOnly = withConfig((c) => {
      setUp(c);
      c.safety!.http.enabled = false;
      c.safety!.mqtt.enabled = false;
    });
    const item = glanceItems(input({ config: weatherOnly })).find((i) => i.id === 'setup');
    expect(item).toMatchObject({ label: 'Finish setup: Weather check', state: '1 left', severity: 'note' });
    expect(item?.fix).toMatchObject({ href: expect.stringContaining('/settings?tab=roof&section=safety') });
    const all = withConfig(setUp);
    expect(ids({ config: all })).not.toContain('setup');
  });

  it('orders problems by the spec priority', () => {
    const status = withStatus((s) => (s.time.epoch = 0));
    const config = withConfig((c) => (c.alerts!.enabled = false));
    const order = glanceItems(input({ status, config })).map((item) => item.id);
    expect(order.indexOf('alerts-state')).toBeLessThan(order.indexOf('clock-location'));
  });
  // Spec 009 FR-015, FR-016: the roof's problems, not its state (DS-07, DS-08).
  it('lists a latched roof fault with its text behind "?"', () => {
    // inventory: roof-fault
    const status = withStatus((s) => {
      s.roof!.fault = { kind: 'timeout', cause: 'logic', measured: 61, limit: 60, text: 'No target magnet after 61 s >= 60 s timeout' };
    });
    const fault = glanceItems(input({ status })).find((i) => i.id === 'roof-fault');
    expect(fault).toMatchObject({
      severity: 'problem',
      label: 'Roof',
      state: 'Fault',
      detail: 'No target magnet after 61 s >= 60 s timeout',
    });
    expect(ids({})).not.toContain('roof-fault');
  });

  it('lists an automatic close that is held, can\u2019t move or has halted', () => {
    // inventory: roof-close
    const phase = (closePhase: 'idle' | 'waitPark' | 'closing' | 'retryWait' | 'heldByPerson' | 'cannotMove' | 'halted') =>
      glanceItems(input({ status: withStatus((s) => (s.roof!.closePhase = closePhase)) })).find((i) => i.id === 'roof-close');
    expect(phase('heldByPerson')).toMatchObject({ severity: 'note', state: 'Held', label: 'Automatic close' });
    expect(phase('cannotMove')).toMatchObject({ severity: 'problem', state: "Can't move" });
    expect(phase('halted')).toMatchObject({ severity: 'problem', state: 'Halted' });
    for (const quiet of ['idle', 'waitPark', 'closing', 'retryWait'] as const) expect(phase(quiet)).toBeUndefined();
  });

  it('a healthy roof adds nothing to check', () => {
    expect(toCheck(glanceItems(input({})))).toHaveLength(0);
  });

  it('a secret SQMeter has and RoofRoof lacks is a note per secret, never a problem (spec 020)', () => {
    // inventory: sqmeter-needs
    const status = withStatus((s) => {
      s.sync!.mqtt = { follow: true, syncedAt: 1, refused: null, needs: ['mqttPassword'] };
      s.sync!.alerts = { follow: true, syncedAt: 1, refused: null, needs: ['ntfyToken', 'pushoverKeys'] };
    });
    const items = glanceItems(input({ status })).filter((item) => item.id === 'sqmeter-needs');
    expect(items.map((item) => [item.label, item.severity])).toEqual([
      ['MQTT password', 'note'],
      ['ntfy token', 'note'],
      ['Pushover keys', 'note'],
    ]);
    expect(items[0].fix).toEqual(expect.objectContaining({ label: 'Open settings' }));
    expect(ids({})).not.toContain('sqmeter-needs');
  });

  it('a test box bypass in effect is a note row, so the card never says All good (spec 022)', () => {
    // inventory: test-box
    const status = withStatus((s) => {
      s.roof!.testBox = { ...s.roof!.testBox!, set: true, active: true, park: true };
    });
    const row = glanceItems(input({ status })).find((item) => item.id === 'test-box');
    expect(row).toMatchObject({ severity: 'note', state: 'Bypassed', fix: expect.objectContaining({ label: 'Manage' }) });
    expect(ids({})).not.toContain('test-box');
  });
});
