import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
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
  'roof.calibrationLimitSeconds',
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
  'roof.blindClose',
  'roof.blindSagVolts',
  'roof.endPlausiblePercent',
  'roof.endOverrunPercent',
  'roof.motionSagVolts',
  'roof.motionCheckSeconds',
  'safety.http.enabled',
  'safety.http.url',
  'safety.mqtt.enabled',
  'safety.staleSeconds',
  'park.nina.url',
  'park.source',
  'park.timeoutSeconds',
  'daylight.mode',
  'daylight.sunAltitudeDeg',
  'location.set',
  'autoClose.rearmMinutes',
  'autoClose.remindMinutes',
  'autoClose.notStarted',
  'autoClose.remoteStop',
  'autoClose.unsafeHoldSeconds',
  'autoClose.ninaLostClose',
  'ui.confirmOpenClose',
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

  it('has the end and movement checks, and warns below a 40 % slow duty (spec 036)', async () => {
    mockDevice();
    await openRoofTab();
    for (const path of ['roof.endPlausiblePercent', 'roof.endOverrunPercent', 'roof.motionSagVolts', 'roof.motionCheckSeconds'])
      expect(await findField(path), path).not.toBeNull();
    expect(screen.queryByText(/Slow duty under 40 %/)).toBeNull();
    fireEvent.input(field('roof.slowDutyPercent')!, { target: { value: '25' } });
    fireEvent.change(field('roof.slowDutyPercent')!, { target: { value: '25' } });
    expect(await screen.findByText(/Slow duty under 40 %/)).toBeInTheDocument();
  });

  it('has the Daylight card: mode, limit, location; latitude and longitude once the location is set (spec 013)', async () => {
    mockDevice();
    await openRoofTab();
    await findField('daylight.mode');
    expect(document.getElementById('daylight')).not.toBeNull();
    expect(Array.from((field('daylight.mode') as unknown as HTMLSelectElement).options).map((o) => o.textContent)).toEqual([
      'Off',
      'Alert',
      'Alert and close',
    ]);
    expect(field('location.latitude')).toBeNull();
    fireEvent.click(screen.getByLabelText('Location set'));
    expect(field('location.latitude')).not.toBeNull();
    expect(field('location.longitude')).not.toBeNull();
  });

  it('refuses a sun altitude, latitude and longitude outside the device ranges', async () => {
    mockDevice({ config: { location: { set: true, latitude: 0, longitude: 0 } } });
    await openRoofTab();
    fireEvent.change(await findField('daylight.sunAltitudeDeg'), { target: { value: '31' } });
    expect(await screen.findByText('Sun altitude must be between -18 and 30 degrees')).toBeInTheDocument();
    fireEvent.change(field('location.latitude')!, { target: { value: '91' } });
    expect(await screen.findByText('Latitude must be between -90 and 90 degrees')).toBeInTheDocument();
    fireEvent.change(field('location.longitude')!, { target: { value: '-181' } });
    expect(await screen.findByText('Longitude must be between -180 and 180 degrees')).toBeInTheDocument();
  });

  it('says why the daylight check is not in effect: no location, and daylight off for the rest (D-42, D-44)', async () => {
    mockDevice();
    await openRoofTab();
    await findField('daylight.mode');
    expect(await waitFor(() => document.querySelector('[data-dep="D-42"]'))).toHaveTextContent('Location not set');
    fireEvent.change(field('daylight.mode')!, { target: { value: 'off' } });
    expect(await waitFor(() => document.querySelector('[data-dep="D-44"]'))).toHaveTextContent('Daylight is off');
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

  it('saves only the edited roof setting (spec 021)', async () => {
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
    expect(sent).toEqual({ roof: { travelMm: 1900 } });
  });

  it('saves the daylight mode, the limit and the location', async () => {
    mockDevice();
    let sent: Config | null = null;
    server.use(
      http.post('/api/config', async ({ request }) => {
        sent = (await request.json()) as Config;
        return HttpResponse.json({ success: true });
      }),
    );
    await openRoofTab();
    fireEvent.change(await findField('daylight.mode'), { target: { value: 'close' } });
    fireEvent.change(field('daylight.sunAltitudeDeg')!, { target: { value: '-6' } });
    fireEvent.click(screen.getByLabelText('Location set'));
    fireEvent.change(await findField('location.latitude'), { target: { value: '51.5' } });
    fireEvent.change(field('location.longitude')!, { target: { value: '-0.12' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(sent).not.toBeNull());
    expect(sent!.daylight).toEqual({ mode: 'close', sunAltitudeDeg: -6 });
    expect(sent!.location).toMatchObject({ set: true, latitude: 51.5, longitude: -0.12 });
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
  // Spec 014: the automatic close's policy, the Advanced folds and the Alpaca note.
  it('shows the automatic close settings with the rest under Advanced, and saves them', async () => {
    mockDevice();
    let sent: Config | null = null;
    server.use(
      http.post('/api/config', async ({ request }) => {
        sent = (await request.json()) as Config;
        return HttpResponse.json({ success: true });
      }),
    );
    await openRoofTab();
    expect(await screen.findByRole('heading', { name: 'Automatic close' })).toBeInTheDocument();
    const fold = (await findField('autoClose.advanced')) as unknown as HTMLDetailsElement;
    expect(fold.tagName).toBe('DETAILS');
    expect(fold.open).toBe(false);
    for (const path of [
      'autoClose.parkRequestSeconds',
      'autoClose.parkHoldSeconds',
      'autoClose.retryMinutes',
      'autoClose.closeStartSeconds',
      'autoClose.closeGuardSeconds',
      'mqtt.changeMinSeconds',
    ])
      expect(fold.querySelector(`[data-field="${path}"]`)).not.toBeNull();
    fireEvent.change(field('autoClose.rearmMinutes')!, { target: { value: '60' } });
    fireEvent.change(field('autoClose.notStarted')!, { target: { value: 'retry' } });
    fireEvent.change(field('autoClose.parkHoldSeconds')!, { target: { value: '30' } });
    fireEvent.click(screen.getByLabelText('Ask before Open and Close'));
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(sent).not.toBeNull());
    expect(sent!.autoClose).toEqual({ rearmMinutes: 60, notStarted: 'retry', parkHoldSeconds: 30 });
    expect(sent!.ui).toEqual({ confirmOpenClose: true });
  });

  it('opens the fold on an out-of-range value and refuses the save', async () => {
    mockDevice();
    await openRoofTab();
    const hold = await findField('autoClose.parkHoldSeconds');
    fireEvent.change(hold, { target: { value: '3' } });
    expect(await screen.findByText('Park hold must be between 4 and 30 s')).toBeInTheDocument();
    expect((field('autoClose.advanced') as unknown as HTMLDetailsElement).open).toBe(true);
  });

  it('says imaging apps cannot open or close the roof while Alpaca is off', async () => {
    mockDevice();
    server.use(http.get('/api/config', () => HttpResponse.json({ ...mockConfig, alpaca: { enabled: false } })));
    await openRoofTab();
    expect(await screen.findByText(/Alpaca is off: imaging apps can't open or close the roof/)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Alpaca settings' })).toBeInTheDocument();
    // The same docs anchor as the setup wizard's N.I.N.A. page.
    expect(screen.getByRole('link', { name: 'How N.I.N.A. opens and closes the roof' }).getAttribute('href')).toMatch(
      /docs\/user-guide\/alpaca\.md#nina-opens-and-closes-the-roof$/,
    );
  });

  it('shows no Alpaca note while Alpaca is on', async () => {
    mockDevice();
    server.use(http.get('/api/config', () => HttpResponse.json({ ...mockConfig, alpaca: { enabled: true } })));
    await openRoofTab();
    expect(await screen.findByRole('heading', { name: 'Automatic close' })).toBeInTheDocument();
    expect(screen.queryByText(/Alpaca is off: imaging apps can't open or close the roof/)).toBeNull();
  });

  it('keeps the daylight band under Advanced in the Daylight card', async () => {
    mockDevice();
    await openRoofTab();
    const fold = (await findField('daylight.advanced')) as unknown as HTMLDetailsElement;
    expect(fold.querySelector('[data-field="daylight.bandDeg"]')).not.toBeNull();
    expect(fold.open).toBe(false);
  });
});

describe('toConfigPayload', () => {
  it('carries the roof, weather and park sections when the device sent them, and never invents them', () => {
    const full = toConfigPayload(mockConfig);
    expect(full.roof).toEqual(mockConfig.roof);
    expect(full.safety).toEqual(mockConfig.safety);
    expect(full.park).toEqual(mockConfig.park);
    expect(full.daylight).toEqual(mockConfig.daylight);
    expect(full.location).toEqual(mockConfig.location);
    expect(full.autoClose).toEqual(mockConfig.autoClose);
    expect(full.ui).toEqual(mockConfig.ui);
    const { roof: _roof, safety: _safety, park: _park, daylight: _daylight, location: _location, ...older } = mockConfig;
    const payload = toConfigPayload(older as Config);
    expect('roof' in payload || 'safety' in payload || 'park' in payload || 'daylight' in payload || 'location' in payload).toBe(false);
  });
});

// Spec 026: travel, speed and the move time limits they make, who set them, and the calibration run's link.
describe('Settings → Roof → Travel and time limits', () => {
  it('shows the move time limits beside travel and speed, says they are defaults, and keeps the run limit under Advanced', async () => {
    mockDevice();
    await openRoofTab();
    const card = document.getElementById('travel')!;
    await waitFor(() => expect(card).not.toBeNull());
    const limits = card.querySelector('[data-move-limits]')!;
    expect(limits).toHaveTextContent('Open or close limit');
    expect(limits).toHaveTextContent('Slow-duty limit (slow speed)');
    expect(card).toHaveTextContent('Travel and speed are still the defaults: run the calibration run.');
    expect(within(card).getByRole('button', { name: 'Calibration run' })).toBeInTheDocument();
    const limit = await findField('roof.calibrationLimitSeconds');
    expect(limit.closest('details')).not.toBeNull();
  });

  it('lists the setup stages first, in order, each with its state', async () => {
    mockDevice();
    await openRoofTab();
    const stages = await waitFor(() => {
      const el = document.querySelector('#setup [data-setup-stages]');
      if (!el) throw new Error('no stages yet');
      return el;
    });
    expect([...stages.querySelectorAll('[data-stage]')].map((row) => row.getAttribute('data-stage'))).toEqual([
      'board',
      'weather',
      'park',
      'direction',
      'ends',
      'calibrated',
      'alerts',
    ]);
    expect(stages.querySelector('[data-stage="calibrated"]')).toHaveAttribute('data-state', 'todo');
  });
});

describe('Settings → Roof → park source', () => {
  // Spec 024: the park source; the switch count only for a switch source, always saved with the source.
  it('sets the park source with the switch count, warns about one switch only, and saves both together', async () => {
    mockDevice();
    const saves: Config[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        saves.push((await request.json()) as Config);
        return HttpResponse.json({ success: true });
      }),
    );
    await openRoofTab();
    const source = (await findField('park.source')) as unknown as HTMLSelectElement;
    expect(source.value).toBe('nina');
    expect(field('park.switches')).toBeNull();
    fireEvent.change(source, { target: { value: 'switches' } });
    const count = (await findField('park.switches')) as unknown as HTMLSelectElement;
    expect(count.value).toBe('one'); // none was fitted
    expect(Array.from(count.options).map((o) => o.value)).toEqual(['one', 'two']);
    expect(document.querySelector('[data-park-note="oneSwitch"]')).toHaveTextContent(/fit two on separate cables/);
    expect(document.querySelector('[data-park-note="wiring"]')).toHaveTextContent(/3 roof openings/);
    expect(screen.getByText(/Address optional with switches only/)).toBeInTheDocument();
    fireEvent.change(count, { target: { value: 'two' } });
    expect(document.querySelector('[data-park-note="oneSwitch"]')).toBeNull();
    fireEvent.change(field('park.source')!, { target: { value: 'ninaAndSwitches' } });
    expect(document.querySelector('[data-park-note="wiring"]')).toBeNull();
    expect((field('park.switches') as unknown as HTMLSelectElement).value).toBe('two'); // kept
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].park).toEqual({ source: 'ninaAndSwitches', switches: 'two' });
  });

  it('choosing N.I.N.A. only fits no switch; a switch count change still sends the source', async () => {
    mockDevice({ config: { park: { ...mockConfig.park!, source: undefined, switches: 'one' } } });
    const saves: Config[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        saves.push((await request.json()) as Config);
        return HttpResponse.json({ success: true });
      }),
    );
    await openRoofTab();
    // Settings from before spec 024: one switch reads as N.I.N.A. and park switches.
    expect(((await findField('park.source')) as unknown as HTMLSelectElement).value).toBe('ninaAndSwitches');
    fireEvent.change(field('park.switches')!, { target: { value: 'two' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].park).toEqual({ source: 'ninaAndSwitches', switches: 'two' });
    fireEvent.change(field('park.source')!, { target: { value: 'nina' } });
    expect(field('park.switches')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(2));
    expect(saves[1].park).toEqual({ source: 'nina', switches: 'none' });
  });

  it('choosing park switches only turns the test box park bypass off, saying so', async () => {
    mockDevice({
      config: { park: { ...mockConfig.park!, nina: { url: '', api: 'v2' } }, testBox: { ...mockConfig.testBox!, park: true } },
    });
    const saves: Config[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        saves.push((await request.json()) as Config);
        return HttpResponse.json({ success: true });
      }),
    );
    await openRoofTab();
    fireEvent.change(await findField('park.source'), { target: { value: 'switches' } });
    expect(document.querySelector('[data-park-note="testBoxCleared"]')).toHaveTextContent(/park bypass is now off/);
    expect(document.querySelector('[data-park-note="testBox"]')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].testBox).toEqual({ park: false });
    expect(saves[0].park).toEqual({ source: 'switches', switches: 'one' });
  });
});
