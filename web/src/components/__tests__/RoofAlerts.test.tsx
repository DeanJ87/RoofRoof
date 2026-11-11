import { describe, it, expect } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Settings from '../Settings';
import { server } from '../../test/mswServer';
import { mockConfig } from '../../mocks/config';
import { alertsConfigSchema } from '../../validation/alertsSchema';
import { EVENT_VARS } from '../settings/alertVariables';
import { defaultText, soundOptions } from '../settings/alertsShared';
import { mergeAlertsConfig } from '../settings/defaults';
import { evaluate, viewOf } from '../../lib/settingsDeps';
import type { AlertEventKey, Config } from '../../types';

// The roof's alert events on Settings > Alerts (spec 007): the critical two
// can't go below Urgent and stay editable with alerts off; the others follow
// the master switch.

const ROOF_KEYS: AlertEventKey[] = [
  'mount_not_parked',
  'roof_not_closed',
  'move_fault',
  'supply_low',
  'unsafe_closing',
  'roof_closed',
  'safety_lost',
  'daylight_open',
  'daylight_closed',
  'daylight_unknown',
];

const options = (select: HTMLSelectElement) => Array.from(select.options).map((o) => o.value);

describe('Roof alert events', () => {
  it('shows a row for every roof event, the critical ones with Urgent and Wake me only', async () => {
    window.history.replaceState(null, '', '/settings?tab=alerts');
    render(<Settings />);
    await screen.findByLabelText("The mount doesn't park: level");
    for (const key of ROOF_KEYS) expect(document.querySelector(`[data-event="${key}"]`)).not.toBeNull();
    expect(options(screen.getByLabelText("The mount doesn't park: level") as HTMLSelectElement)).toEqual(['3', '4']);
    expect(options(screen.getByLabelText("The roof doesn't close: level") as HTMLSelectElement)).toEqual(['3', '4']);
    expect(options(screen.getByLabelText('The roof faults: level') as HTMLSelectElement)).toEqual(['0', '1', '2', '3', '4']);
  });

  it('keeps the critical rows editable while alerts are off, and saves them', async () => {
    let saved: any = null;
    server.use(
      http.get('/api/config', () => HttpResponse.json({ ...mockConfig, alerts: { ...mockConfig.alerts, enabled: false } })),
      http.post('/api/config', async ({ request }) => {
        saved = await request.json();
        return HttpResponse.json({ success: true });
      }),
    );
    window.history.replaceState(null, '', '/settings?tab=alerts');
    render(<Settings />);
    const critical = (await screen.findByLabelText("The mount doesn't park: level")) as HTMLSelectElement;
    expect(critical.disabled).toBe(false);
    expect((screen.getByLabelText('The roof faults: level') as HTMLSelectElement).disabled).toBe(true);
    const row = document.querySelector('[data-event="mount_not_parked"]') as HTMLElement;
    expect(within(row).getByRole('button', { name: 'Text' })).not.toBeDisabled();
    fireEvent.change(critical, { target: { value: '3' } });
    fireEvent.click(screen.getByRole('button', { name: /save/i }));
    await waitFor(() => expect(saved).not.toBeNull());
    expect(saved.alerts.events.mount_not_parked).toMatchObject({ level: 3 });
  });

  it('refuses a critical level below urgent', () => {
    const alerts = mergeAlertsConfig(mockConfig.alerts);
    const withLevel = (key: AlertEventKey, level: number) =>
      alertsConfigSchema.safeParse({ ...alerts, events: { ...alerts.events, [key]: { level, sound: '' } } }).success;
    for (const key of ['mount_not_parked', 'roof_not_closed'] as const) {
      expect(withLevel(key, 0)).toBe(false);
      expect(withLevel(key, 2)).toBe(false);
      expect(withLevel(key, 3)).toBe(true);
      expect(withLevel(key, 4)).toBe(true);
    }
    expect(withLevel('move_fault', 0)).toBe(true);
    const withSound = (key: AlertEventKey, sound: string) =>
      alertsConfigSchema.safeParse({ ...alerts, events: { ...alerts.events, [key]: { level: 4, sound } } }).success;
    expect(withSound('mount_not_parked', 'none')).toBe(false);
    expect(withSound('roof_not_closed', 'vibrate')).toBe(false);
    expect(withSound('roof_not_closed', 'siren')).toBe(true);
    expect(withSound('move_fault', 'none')).toBe(true);
  });

  it('offers no silent sound for the critical events', () => {
    const values = soundOptions('', 'Default', true).map((o) => o.value);
    expect(values).not.toContain('none');
    expect(values).not.toContain('vibrate');
    expect(soundOptions('', 'Default').map((o) => o.value)).toContain('none');
  });

  it('offers {reason} for every roof event and the built-in wording as the placeholder', () => {
    for (const key of ROOF_KEYS) {
      expect(EVENT_VARS[key]).toContain('reason');
      expect(defaultText(key).title.length).toBeGreaterThan(0);
    }
    expect(EVENT_VARS.mount_not_parked).toEqual(['reason', 'limit', 'details']);
    expect(defaultText('move_fault').message).toContain('{reason}');
  });

  it('has the daylight events: variables, wording and defaults (spec 013)', () => {
    expect(EVENT_VARS.daylight_open).toEqual(['reason', 'action']);
    expect(EVENT_VARS.daylight_closed).toEqual(['reason']);
    expect(EVENT_VARS.daylight_unknown).toEqual(['reason']);
    expect(defaultText('daylight_open')).toEqual({
      title: 'Roof open in daylight',
      message: "The sun is up and the roof isn't closed. {action} Sun: {reason}",
    });
    expect(defaultText('daylight_closed').title).toBe('Roof closed after daylight');
    expect(defaultText('daylight_unknown').title).toBe('Daylight check not working');
    const events = mergeAlertsConfig(undefined).events;
    expect(events.daylight_open.level).toBe(3);
    expect(events.daylight_closed.level).toBe(2);
    expect(events.daylight_unknown.level).toBe(2);
  });

  it('waits on alerts first, then on daylight, for the daylight events', () => {
    const facts = { wifiConnected: true, mqttConnected: true, clockSet: true, alpacaDevices: 1 };
    const withAlerts = { ...mockConfig, alerts: mergeAlertsConfig({ ...mockConfig.alerts, enabled: true }) };
    const off = viewOf(evaluate({ ...withAlerts, daylight: { mode: 'off', sunAltitudeDeg: 0 } }, facts));
    expect(off.get('alerts.events.daylight_open.level')).toMatchObject({ id: 'D-44', state: 'inactive', reason: 'daylight-off' });
    const noAlerts = viewOf(
      evaluate(
        {
          ...mockConfig,
          alerts: mergeAlertsConfig({ ...mockConfig.alerts, enabled: false }),
          daylight: { mode: 'off', sunAltitudeDeg: 0 },
        },
        facts,
      ),
    );
    expect(noAlerts.get('alerts.events.daylight_open.level')).toMatchObject({ id: 'D-04', reason: 'alerts-off' });
    const on = viewOf(evaluate({ ...withAlerts, daylight: { mode: 'close', sunAltitudeDeg: 0 } }, facts));
    expect(on.get('alerts.events.daylight_closed.level').state).toBe('active');
  });

  it('lists the non-critical roof levels under alerts on, and not the critical ones', () => {
    const config: Config = { ...mockConfig, alerts: mergeAlertsConfig({ ...mockConfig.alerts, enabled: false }) };
    const view = viewOf(evaluate(config, { wifiConnected: true, mqttConnected: true, clockSet: true, alpacaDevices: 1 }));
    for (const key of ['move_fault', 'supply_low', 'unsafe_closing', 'roof_closed', 'safety_lost'])
      expect(view.get(`alerts.events.${key}.level`)).toMatchObject({ id: 'D-04', state: 'inactive', reason: 'alerts-off' });
    expect(view.get('alerts.events.mount_not_parked.level').state).toBe('unknown');
  });
});
