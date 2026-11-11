import { describe, it, expect } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Settings from '../Settings';
import { server } from '../../test/mswServer';
import { mockConfig } from '../../mocks/config';
import { alertsConfigSchema } from '../../validation/alertsSchema';
import { EVENT_VARS } from '../settings/alertVariables';
import { defaultText } from '../settings/alertsShared';
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
  });

  it('offers {reason} for every roof event and the built-in wording as the placeholder', () => {
    for (const key of ROOF_KEYS) {
      expect(EVENT_VARS[key]).toContain('reason');
      expect(defaultText(key).title.length).toBeGreaterThan(0);
    }
    expect(EVENT_VARS.mount_not_parked).toEqual(['reason', 'limit', 'details']);
    expect(defaultText('move_fault').message).toContain('{reason}');
  });

  it('lists the non-critical roof levels under alerts on, and not the critical ones', () => {
    const config: Config = { ...mockConfig, alerts: mergeAlertsConfig({ ...mockConfig.alerts, enabled: false }) };
    const view = viewOf(evaluate(config, { wifiConnected: true, mqttConnected: true, clockSet: true, alpacaDevices: 1 }));
    for (const key of ['move_fault', 'supply_low', 'unsafe_closing', 'roof_closed', 'safety_lost'])
      expect(view.get(`alerts.events.${key}.level`)).toMatchObject({ id: 'D-04', state: 'inactive', reason: 'alerts-off' });
    expect(view.get('alerts.events.mount_not_parked.level').state).toBe('unknown');
  });
});
