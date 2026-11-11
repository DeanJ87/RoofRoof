import { describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import { server } from '../../test/mswServer';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { RoofStatus, SystemStatus } from '../../types';
import type { LiveState } from '../../lib/roofView';
import RoofCard from '../RoofCard';

// The Roof card as rendered (spec 009 US1, US3; contracts/ui.md), every state
// driven directly. The demo's simulated roof (spec 011) shows the main ones in
// Playwright (web/tests/dashboard.spec.ts).

const status = (roof: Partial<RoofStatus> = {}, apMode = false): SystemStatus => {
  const s = structuredClone(mockStatus);
  s.roof = { ...s.roof!, ...roof };
  s.wifi = { ...s.wifi, apMode };
  return s;
};
const live = (s: SystemStatus | null, over: Partial<LiveState> = {}): LiveState => ({
  status: s,
  connected: true,
  quiet: false,
  reconnectedAt: null,
  ...over,
});
const show = (state: LiveState) => render(<RoofCard live={state} config={mockConfig.roof} />);
const button = (name: string) => screen.getByRole('button', { name });
const FAULT = { kind: 'timeout', cause: 'logic' as const, measured: 61, limit: 60, text: 'No target magnet after 61 s >= 60 s timeout' };

describe('RoofCard', () => {
  it('shows a closed roof: state, position, Open enabled, Close disabled with its reason', () => {
    // inventory: roof-card
    show(live(status()));
    expect(screen.getByRole('heading', { name: 'Roof' })).toBeInTheDocument();
    expect(screen.getAllByText('Closed')[0].classList.contains('pill')).toBe(true);
    expect(screen.getByRole('meter', { name: 'Position' })).toHaveAttribute('aria-valuetext', '0% open');
    expect(button('Open')).toBeEnabled();
    expect(button('Close')).toBeDisabled();
    const reason = document.getElementById(button('Close').getAttribute('aria-describedby')!);
    expect(reason).toHaveTextContent('Close: Already closed');
    expect(button('Stop')).toBeEnabled();
  });

  it('shows the daylight line with the mode behind the hint, and why Open is off (spec 013)', () => {
    // inventory: roof-daylight
    const daylight = {
      mode: 'close' as const,
      state: 'day' as const,
      sunAltitudeDeg: 3.2,
      limitDeg: 0,
      text: 'Sun 3.2° ≥ 0.0°',
      closes: true,
    };
    show(live(status({ daylight })));
    const row = document.querySelector('[data-inventory="roof-daylight"]')!;
    expect(within(row as HTMLElement).getByText('Daylight')).toBeInTheDocument();
    expect(row).toHaveTextContent('Sun 3.2° ≥ 0.0°');
    expect(row.querySelector('[role="tooltip"], .info-tip, button')).not.toBeNull();
    expect(button('Open')).toBeDisabled();
    expect(screen.getByText('Open: Daylight (Sun 3.2° ≥ 0.0°)')).toBeInTheDocument();
  });

  it('shows the daylight line as Off, and leaves it out for older firmware', () => {
    // inventory: roof-daylight
    const off = { mode: 'off' as const, state: 'off' as const, sunAltitudeDeg: null, limitDeg: 0, text: 'Off', closes: false };
    const { unmount } = show(live(status({ daylight: off })));
    expect(document.querySelector('[data-inventory="roof-daylight"]')).toHaveTextContent('Off');
    expect(button('Open')).toBeEnabled();
    unmount();
    show(live(status({ daylight: undefined })));
    expect(document.querySelector('[data-inventory="roof-daylight"]')).toBeNull();
  });

  it('never disables Stop, also with no live data, and says why Open and Close are', () => {
    // inventory: roof-buttons
    show(live(status({ state: 'opening' }), { connected: false }));
    expect(button('Stop')).toBeEnabled();
    expect(button('Stop')).not.toHaveAttribute('disabled');
    expect(button('Open')).toBeDisabled();
    expect(button('Close')).toBeDisabled();
    // One note for the reason both share; both buttons point at it.
    expect(screen.getAllByText('Open: No live data')).toHaveLength(1);
    expect(button('Close').getAttribute('aria-describedby')).toBe(button('Open').getAttribute('aria-describedby'));
  });

  it('sends Stop at once, every press, with the header', async () => {
    const seen: (string | null)[] = [];
    server.use(
      http.post('/api/roof/stop', ({ request }) => {
        seen.push(request.headers.get('X-RoofRoof'));
        return HttpResponse.json({ success: true, state: 'stopped' });
      }),
    );
    show(live(status({ state: 'opening' })));
    fireEvent.click(button('Stop'));
    fireEvent.click(button('Stop'));
    await waitFor(() => expect(seen).toEqual(['1', '1']));
  });

  it('shows the device’s refusal under the buttons', async () => {
    server.use(
      http.post('/api/roof/open', () =>
        HttpResponse.json({ success: false, state: 'closed', reason: 'busy', error: 'A settings save is being written' }, { status: 409 }),
      ),
    );
    show(live(status()));
    fireEvent.click(button('Open'));
    expect(await screen.findByText('A settings save is being written')).toBeInTheDocument();
    // busy: try again later, the button stays usable.
    expect(button('Open')).toBeEnabled();
  });

  it('holds Open and Close after a restarting refusal', async () => {
    server.use(
      http.post('/api/roof/open', () =>
        HttpResponse.json({ success: false, state: 'closed', reason: 'restarting', error: 'The device is restarting' }, { status: 409 }),
      ),
    );
    show(live(status({ state: 'stopped' })));
    fireEvent.click(button('Open'));
    expect(await screen.findByText('Open: The device is restarting')).toBeInTheDocument();
    expect(button('Close')).toBeDisabled();
    expect(button('Stop')).toBeEnabled();
  });

  it('a latched fault: its text, Clear fault, and Open and Close disabled', async () => {
    // inventory: roof-fault-card
    let cleared = false;
    server.use(
      http.post('/api/roof/clear-fault', ({ request }) => {
        cleared = request.headers.get('X-RoofRoof') === '1';
        return HttpResponse.json({ success: true, state: 'unknown' });
      }),
    );
    show(live(status({ state: 'error', fault: FAULT })));
    expect(screen.getByText('No target magnet after 61 s >= 60 s timeout')).toBeInTheDocument();
    expect(button('Open')).toBeDisabled();
    expect(screen.getByText('Open: Fault: clear it first')).toBeInTheDocument();
    fireEvent.click(button('Clear fault'));
    await waitFor(() => expect(cleared).toBe(true));
  });

  it('Clear fault is disabled without live data, with the reason', () => {
    show(live(status({ state: 'error', fault: FAULT }), { quiet: true }));
    expect(button('Clear fault')).toBeDisabled();
    expect(screen.getByText('Clear fault: No live data')).toBeInTheDocument();
    expect(button('Stop')).toBeEnabled();
  });

  it('shows the weather, the park and the automatic close with their reasons', () => {
    // inventory: roof-checks  inventory: roof-close-phase
    show(
      live(
        status({
          state: 'open',
          weather: 'unsafe',
          weatherText: 'unsafe: rain (SQMeter HTTP)',
          parkConfirmed: false,
          parkText: 'Mount not parked: Park switch 1 open',
          closePhase: 'waitPark',
        }),
      ),
    );
    const checks = document.querySelector('[data-inventory="roof-checks"]') as HTMLElement;
    expect(within(checks).getByText('Unsafe')).toBeInTheDocument();
    expect(within(checks).getByText('Not parked')).toBeInTheDocument();
    expect(within(checks).getByText('Waiting for park')).toBeInTheDocument();
    expect(screen.getByText('Close: Mount not parked: Park switch 1 open')).toBeInTheDocument();
    expect(screen.getByText('Stop calls off the automatic close')).toBeInTheDocument();
  });

  it('shows the readings that are fitted or on, and only those', () => {
    // inventory: roof-readings
    const { unmount } = show(live(status()));
    expect(screen.getByText('Supply')).toBeInTheDocument();
    expect(screen.queryByText('Motor current')).toBeNull();
    expect(screen.queryByText('Park switch 1')).toBeNull();
    expect(screen.queryByText('Rain contact')).toBeNull();
    unmount();
    show(
      live(
        status({
          currentSense: true,
          motorAmps: 3.2,
          parkSwitches: { fitted: 2, switch1: 'closed', switch2: 'open' },
          rainContact: 'dry',
          supplyLow: true,
          supplyVolts: 10.9,
          minSupplyVolts: 11.5,
        }),
      ),
    );
    expect(screen.getByText('3.2 A')).toBeInTheDocument();
    // The limits are behind "?" (FR-011).
    expect(screen.getByText("Minimum 11.5 V: below it the roof won't start, and a move stops with a fault.")).toBeInTheDocument();
    expect(screen.getByText('Limit 10.0 A: above it a move stops with a fault.')).toBeInTheDocument();
    expect(screen.getByText('Park switch 2')).toBeInTheDocument();
    expect(screen.getByText('Dry')).toBeInTheDocument();
    expect(screen.getByText('Open: Supply 10.9 V is below 11.5 V')).toBeInTheDocument();
  });

  it('draws the Hall reading against the saved thresholds', () => {
    // inventory: roof-hall
    show(live(status({ hall: { level: 'low', counts: 712 } })));
    expect(screen.getByRole('img', { name: 'Hall reading 712 of 4095; low threshold 1,100, high threshold 3,000' })).toBeInTheDocument();
    expect(screen.getByText('At the low magnet')).toBeInTheDocument();
  });

  it('says when the position is unknown, and keeps Open usable', () => {
    // inventory: roof-position
    show(live(status({ state: 'unknown', positionKnown: false, positionPercent: null, positionMm: null })));
    expect(screen.getByRole('meter', { name: 'Position' })).toHaveAttribute('aria-valuetext', 'Unknown');
    expect(screen.getByText('Position unknown: a move goes slowly to the first magnet')).toBeInTheDocument();
    expect(button('Open')).toBeEnabled();
  });

  it('a roof that isn’t reporting: Stop still there', () => {
    const s = status();
    delete s.roof;
    show(live(s));
    expect(screen.getByText('Not reporting')).toBeInTheDocument();
    expect(button('Stop')).toBeEnabled();
  });
});
