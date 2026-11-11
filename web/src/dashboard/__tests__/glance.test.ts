import { describe, expect, it } from 'vitest';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { AlertSchedule, Config, SystemStatus } from '../../types';
import type { EffectiveReport } from '../../lib/settingsDeps';
import { glanceItems, toCheck, type GlanceInput } from '../glance';

// Every visibility rule of the Status card (SQMeter specs 025 FR-005..FR-017, 026 FR-002):
// the imaging app and alerts, plus problems no other card shows (DS-08).

const input = (over: Partial<GlanceInput> = {}): GlanceInput => ({
  status: structuredClone(mockStatus),
  config: structuredClone(mockConfig),
  effective: { facts: {} as EffectiveReport['facts'], settings: [] },
  connected: true,
  quiet: false,
  schedule: { armed: true, mode: 'any', reason: 'none', since: null, sinceAgeMs: null },
  ...over,
});

const ids = (over: Partial<GlanceInput>) =>
  glanceItems(input(over))
    .filter((item) => item.severity === 'note' || item.severity === 'problem')
    .map((item) => item.id);

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
    const items = glanceItems(input({ status, config: withConfig((c) => (c.alerts!.enabled = true)) }));
    expect(toCheck(items)).toEqual([]);
    // Tiles: the imaging app per Alpaca device, then Alerts.
    expect(items.map((item) => [item.label, item.state])).toEqual(
      expect.arrayContaining([
        ['Alerts', 'Sending'],
        ['Imaging app', 'Connected'],
      ]),
    );
  });

  it('says when the data is offline or quiet', () => {
    // inventory: freshness
    expect(glanceItems(input({ connected: false }))[0]).toMatchObject({ id: 'freshness', severity: 'problem', state: 'Offline' });
    expect(glanceItems(input({ quiet: true }))[0].state).toBe('No updates');
    expect(glanceItems(input()).some((i) => i.id === 'freshness')).toBe(false);
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

  it('orders problems by the spec priority', () => {
    const status = withStatus((s) => (s.time.epoch = 0));
    const config = withConfig((c) => (c.alerts!.enabled = false));
    const order = glanceItems(input({ connected: false, status, config })).map((item) => item.id);
    expect(order.indexOf('freshness')).toBeLessThan(order.indexOf('alerts-state'));
    expect(order.indexOf('alerts-state')).toBeLessThan(order.indexOf('clock-location'));
  });
});
