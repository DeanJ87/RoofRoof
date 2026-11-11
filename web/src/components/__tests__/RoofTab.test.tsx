import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig, mockStatus } from '../../mocks/data';
import { toConfigPayload } from '../settings/payload';
import type { Config, RoofState } from '../../types';

// Settings → Roof (spec 009 US4, FR-018 to FR-023).

const FIELDS = [
  'roof.travelMm',
  'roof.speedMmPerSecond',
  'roof.timeoutMarginSeconds',
  'roof.leaveMagnetSeconds',
  'roof.rampUpMs',
  'roof.rampDownMs',
  'roof.topDutyPercent',
  'roof.slowDutyPercent',
  'roof.openDirection',
  'roof.hallHighEnd',
  'roof.hallHighCounts',
  'roof.hallLowCounts',
  'roof.currentSenseEnabled',
  'roof.minSupplyVolts',
  'safety.http.enabled',
  'safety.http.url',
  'safety.mqtt.enabled',
  'safety.rainContact',
  'safety.staleSeconds',
  'park.nina.url',
  'park.switches',
  'park.timeoutSeconds',
];

const openRoofTab = async () => {
  window.history.replaceState(null, '', '/settings?tab=roof');
  render(<Toaster />);
  render(<Settings />);
  expect(await screen.findByRole('tab', { name: 'Roof', selected: true })).toBeInTheDocument();
};
const field = (path: string) => document.querySelector<HTMLInputElement>(`[data-field="${path}"]`);
const findField = (path: string) =>
  waitFor(() => {
    const el = field(path);
    if (!el) throw new Error(`no ${path} yet`);
    return el;
  });
const withRoofState = (state: RoofState) =>
  server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, roof: { ...mockStatus.roof!, state } })));

describe('Roof tab', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('has every roof, weather and park setting', async () => {
    mockDevice();
    await openRoofTab();
    await findField('roof.travelMm');
    for (const path of FIELDS) expect(field(path), path).not.toBeNull();
    // The current-sense values show once it is on.
    expect(field('roof.maxCurrentAmps')).toBeNull();
    fireEvent.click(screen.getByLabelText('Current sense'));
    for (const path of ['roof.maxCurrentAmps', 'roof.currentAmpsPerVolt', 'roof.minRunningAmps']) expect(field(path), path).not.toBeNull();
  });

  it('refuses a slow duty above the top duty, next to the field, before saving', async () => {
    mockDevice();
    await openRoofTab();
    const slow = await findField('roof.slowDutyPercent');
    fireEvent.change(slow, { target: { value: '100' } });
    fireEvent.change(field('roof.topDutyPercent')!, { target: { value: '50' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    const message = await screen.findByText("Roof: slow duty can't be above the top duty");
    expect(message.closest('.field')?.querySelector('.field-label')?.textContent).toContain('Slow duty');
  });

  it('refuses a park time limit below 30 s (the device range)', async () => {
    mockDevice();
    await openRoofTab();
    fireEvent.change(await findField('park.timeoutSeconds'), { target: { value: '20' } });
    expect(await screen.findByText('Park: time limit must be between 30 and 1800 seconds')).toBeInTheDocument();
  });

  it('jumps to the Roof tab for a roof error found on save', async () => {
    mockDevice();
    window.history.replaceState(null, '', '/settings?tab=roof');
    render(<Settings />);
    fireEvent.change(await findField('roof.travelMm'), { target: { value: '50' } });
    fireEvent.click(screen.getByRole('tab', { name: 'Device' }));
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(await screen.findByRole('tab', { name: 'Roof', selected: true })).toBeInTheDocument();
  });

  it('saves the edited roof section with the rest', async () => {
    mockDevice();
    let sent: Config | null = null;
    server.use(
      http.post('/api/config', async ({ request }) => {
        sent = (await request.json()) as Config;
        return HttpResponse.json({ success: true });
      }),
    );
    await openRoofTab();
    fireEvent.change(await findField('roof.travelMm'), { target: { value: '1900' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(await screen.findByText('Saved.')).toBeInTheDocument();
    expect(sent!.roof!.travelMm).toBe(1900);
    expect(sent!.park).toEqual(mockConfig.park);
    expect(sent!.safety).toEqual(mockConfig.safety);
  });

  it('says saves are refused while the roof moves, and what a save does while stopped part-way', async () => {
    mockDevice();
    withRoofState('opening');
    await openRoofTab();
    expect(await screen.findByText('The roof is moving: saves are refused until it stops')).toBeInTheDocument();
  });

  it('says a save while stopped part-way makes the position unknown', async () => {
    mockDevice();
    withRoofState('stopped');
    await openRoofTab();
    expect(await screen.findByText("Stopped part-way: saving makes the roof's position unknown")).toBeInTheDocument();
  });

  it('shows the SQMeter MQTT source as not in effect while MQTT is off (D-40)', async () => {
    mockDevice({
      config: {
        mqtt: { ...mockConfig.mqtt, enabled: false },
        safety: { ...mockConfig.safety!, mqtt: { enabled: true, topic: 'sqmeter' } },
      },
    });
    await openRoofTab();
    expect(await waitFor(() => document.querySelector('[data-dep="D-40"]'))).not.toBeNull();
  });

  it('draws the live Hall reading against the thresholds as edited', async () => {
    mockDevice();
    await openRoofTab();
    fireEvent.change(await findField('roof.hallLowCounts'), { target: { value: '900' } });
    expect(await screen.findByRole('img', { name: /low threshold 900, high threshold 3,000/ })).toBeInTheDocument();
  });
});

describe('toConfigPayload', () => {
  it('carries the roof, weather and park sections when the device sent them, and never invents them', () => {
    const full = toConfigPayload(mockConfig);
    expect(full.roof).toEqual(mockConfig.roof);
    expect(full.safety).toEqual(mockConfig.safety);
    expect(full.park).toEqual(mockConfig.park);
    const { roof: _roof, safety: _safety, park: _park, ...older } = mockConfig;
    const payload = toConfigPayload(older as Config);
    expect('roof' in payload || 'safety' in payload || 'park' in payload).toBe(false);
  });
});
