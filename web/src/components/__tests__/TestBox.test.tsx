import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import TestBoxBanner, { testBoxText } from '../TestBoxBanner';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig, mockStatus } from '../../mocks/data';
import { parkPill, weatherPill } from '../../lib/roofView';
import type { RoofStatus, RoofTestBox } from '../../types';

// Spec 022: the test box's bypasses: the banner on every page, the dashboard's
// "Test box bypass", and the Settings → Roof card.

const UNTIL = 1764000000;
const box = (change: Partial<RoofTestBox> = {}): RoofTestBox => ({
  set: true,
  active: true,
  park: true,
  weather: true,
  daylight: false,
  until: UNTIL,
  expired: false,
  waitingForClock: false,
  parkRequests: false,
  text: '',
  ...change,
});
const withBox = (testBox: RoofTestBox) =>
  server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, roof: { ...mockStatus.roof, testBox } })));

describe('TestBoxBanner', () => {
  it('names each bypass in effect and when it ends, in red', async () => {
    withBox(box());
    render(<TestBoxBanner />);
    const banner = await screen.findByRole('status');
    expect(banner).toHaveTextContent(/Test box: park check \(no telescope\), weather check \(no weather source\) bypassed until/);
    expect(banner.querySelector('.note-bad')).not.toBeNull();
  });

  it('says when unsafe weather would ask N.I.N.A. to park its mount', () => {
    expect(testBoxText(box({ parkRequests: true }))).toMatch(/Unsafe weather asks N\.I\.N\.A\. to park its mount\.$/);
    expect(testBoxText(box({ parkRequests: false }))).not.toMatch(/asks N\.I\.N\.A\./);
  });

  it('says when they expired or wait for the clock', () => {
    expect(testBoxText(box({ active: false, park: false, weather: false, expired: true }))).toMatch(/expired at .*every check is back/);
    expect(testBoxText(box({ active: false, park: false, weather: false, waitingForClock: true }))).toMatch(/wait for the clock/);
    expect(testBoxText(box({ active: false, park: false, weather: false }))).toMatch(/not in effect/);
    // A restore brings no deadline: never "expired" in 1970.
    expect(testBoxText(box({ active: false, park: false, weather: false, expired: true, until: 0 }))).toMatch(/not started/);
  });

  it('shows nothing with no bypass set, or on older firmware', async () => {
    expect(testBoxText(undefined)).toBeNull();
    withBox(box({ set: false, active: false, park: false, weather: false }));
    const { container } = render(<TestBoxBanner />);
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(container.querySelector('.test-box-banner')).toBeNull();
  });
});

describe('dashboard pills', () => {
  const roof = (testBox: RoofTestBox): RoofStatus => ({ ...mockStatus.roof!, testBox });
  it('a bypassed check never says Parked or Safe', () => {
    expect(parkPill(roof(box()))).toMatchObject({ label: 'Test box bypass', tone: 'pill-red' });
    expect(weatherPill(roof(box()))).toMatchObject({ label: 'Test box bypass', tone: 'pill-red' });
  });
  it('without a bypass they read as before', () => {
    const off = box({ set: false, active: false, park: false, weather: false });
    expect(parkPill(roof(off)).label).toBe('Parked');
    expect(weatherPill(roof(off)).label).toBe('Safe');
  });
});

describe('Settings → Roof → Test box', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  const openRoofTab = async () => {
    window.history.replaceState(null, '', '/settings?tab=roof');
    render(<Toaster />);
    render(<Settings />);
    expect(await screen.findByRole('tab', { name: 'Roof', selected: true })).toBeInTheDocument();
    return waitFor(() => {
      const el = document.querySelector('[data-field="testBox.park"]');
      if (!el) throw new Error('no card yet');
      return el;
    });
  };

  it('can not switch a bypass on beside the source it stands in for', async () => {
    mockDevice();
    await openRoofTab();
    expect(screen.getByText('Clear the N.I.N.A. address first: this stands in for N.I.N.A.')).toBeInTheDocument();
    expect(screen.getByText('Turn SQMeter HTTP and MQTT off first: this stands in for a missing weather source.')).toBeInTheDocument();
  });

  it('asking N.I.N.A. to park is a switch on the Mount park card, on by default', async () => {
    mockDevice();
    await openRoofTab();
    const toggle = document.querySelector<HTMLInputElement>('[data-field="park.requestPark"] input, input[data-field="park.requestPark"]');
    expect(toggle).not.toBeNull();
    expect(toggle!.checked).toBe(true);
    expect(screen.getAllByText(/Never point a bench box at a live N\.I\.N\.A\./).length).toBeGreaterThan(0);
  });

  it('saves the bypasses; Renew sends only the hours', async () => {
    mockDevice({
      config: {
        park: { ...mockConfig.park!, nina: { url: '', api: 'v2' } },
        safety: { ...mockConfig.safety!, http: { enabled: false, url: '' }, mqtt: { enabled: false, topic: 'sqmeter' } },
        testBox: { park: true, weather: false, daylight: false, hours: 72, until: UNTIL },
      },
    });
    const bodies: unknown[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        bodies.push(await request.json());
        return HttpResponse.json({ success: true, revision: 1 });
      }),
    );
    await openRoofTab();
    fireEvent.click(await screen.findByRole('button', { name: 'Renew' }));
    await waitFor(() => expect(bodies).toEqual([{ testBox: { hours: 72 } }]));
    expect(await screen.findByText('Test box bypasses renewed for 72 h.')).toBeInTheDocument();
  });
});
