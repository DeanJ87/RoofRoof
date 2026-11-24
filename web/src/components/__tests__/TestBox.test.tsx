import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import TestBoxBanner, { formatUntil, testBoxView } from '../TestBoxBanner';
import TestBox from '../TestBox';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig, mockStatus } from '../../mocks/data';
import { parkPill, weatherPill } from '../../lib/roofView';
import type { RoofStatus, RoofTestBox } from '../../types';

// Spec 022: the test box's bypasses: the banner on every page, the dashboard's
// "Test box bypass", the Settings → Roof entry and the test box page.

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
  it('a red banner: a short headline naming each bypass, the deadline and the rules on a second line', async () => {
    withBox(box());
    render(<TestBoxBanner />);
    const banner = await screen.findByRole('status');
    expect(banner).toHaveClass('banner', 'banner-bad');
    expect(banner.querySelector('.banner-title')).toHaveTextContent(/^Test box: park check and weather check bypassed$/);
    expect(banner.querySelector('.banner-detail')).toHaveTextContent(
      new RegExp(`^Ends ${formatUntil(UNTIL)}\\. Faults still stop the roof\\. Turn it off before a telescope goes under\\.$`),
    );
    expect(banner.querySelector('svg.ui-icon')).not.toBeNull();
    expect(screen.getByRole('button', { name: 'Manage test box' })).toHaveTextContent('Manage');
  });

  it('names every bypass in effect, daylight too', () => {
    expect(testBoxView(box({ daylight: true }))?.title).toBe('Test box: park check, weather check and daylight close bypassed');
    expect(testBoxView(box({ weather: false }))?.title).toBe('Test box: park check bypassed');
  });

  it("shows the deadline with its weekday, day, month and time in this browser's format", () => {
    const shown = formatUntil(UNTIL);
    const date = new Date(UNTIL * 1000);
    expect(shown).toContain(String(date.getDate()));
    expect(shown).toMatch(/Mon|Tue|Wed|Thu|Fri|Sat|Sun/);
    expect(testBoxView(box())?.detail).toContain(shown);
  });

  it('says when unsafe weather would ask N.I.N.A. to park its mount', () => {
    expect(testBoxView(box({ parkRequests: true }))?.detail).toMatch(/Unsafe weather asks N\.I\.N\.A\. to park its mount\.$/);
    expect(testBoxView(box({ parkRequests: false }))?.detail).not.toMatch(/asks N\.I\.N\.A\./);
  });

  it('amber, not red, while every check applies: expired, waiting for the clock, not in effect, not started', () => {
    const off = { active: false, park: false, weather: false };
    expect(testBoxView(box({ ...off, expired: true }))).toMatchObject({ tone: 'warn', title: 'Test box: expired' });
    expect(testBoxView(box({ ...off, expired: true }))?.detail).toMatch(/^Every check is back since /);
    expect(testBoxView(box({ ...off, waitingForClock: true }))).toMatchObject({ tone: 'warn', title: 'Test box: waiting for the clock' });
    expect(testBoxView(box(off))).toMatchObject({ tone: 'warn', title: 'Test box: not in effect' });
    // A restore brings no deadline: never "expired" in 1970.
    expect(testBoxView(box({ ...off, expired: true, until: 0 }))).toMatchObject({ tone: 'warn', title: 'Test box: not started' });
  });

  it('shows nothing with no bypass set, or on older firmware', async () => {
    expect(testBoxView(undefined)).toBeNull();
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

  it('only says whether one is set and opens the test box page: no switches here', async () => {
    mockDevice();
    withBox(box());
    window.history.replaceState(null, '', '/settings?tab=roof');
    render(<Settings />);
    const card = await waitFor(() => {
      const el = document.getElementById('testBox');
      if (!el) throw new Error('no card yet');
      return el;
    });
    await waitFor(() => expect(card).toHaveTextContent(`Ends ${formatUntil(UNTIL)}.`));
    expect(within(card).getByText('On')).toHaveClass('pill-red');
    expect(card.querySelector('[data-field="testBox.park"]')).toBeNull();
    expect(within(card).getByRole('button', { name: 'Open test box' })).toBeInTheDocument();
  });

  it('asking N.I.N.A. to park is a switch on the Mount park card, on by default', async () => {
    mockDevice();
    window.history.replaceState(null, '', '/settings?tab=roof');
    render(<Settings />);
    const toggle = await waitFor(() => {
      const el = document.querySelector<HTMLInputElement>('[data-field="park.requestPark"] input, input[data-field="park.requestPark"]');
      if (!el) throw new Error('no card yet');
      return el;
    });
    expect(toggle.checked).toBe(true);
    expect(screen.getAllByText(/Never point a bench box at a live N\.I\.N\.A\./).length).toBeGreaterThan(0);
  });
});

describe('the test box page', () => {
  const openPage = async () => {
    render(<Toaster />);
    render(<TestBox />);
    return waitFor(() => {
      const el = document.querySelector('[data-field="testBox.park"]');
      if (!el) throw new Error('no page yet');
      return el;
    });
  };

  it('explains a blocked park switch in one short line with a link', async () => {
    mockDevice();
    await openPage();
    expect(screen.getByText('Off while a N.I.N.A. address is set.')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Park settings' })).toBeInTheDocument();
  });

  // Spec 026: turning the weather bypass on beside an SQMeter source turns the source off in the same save,
  // so the weather check never reads "no weather source" (unsafe) between two saves and starts a close.
  it('the weather bypass and the weather source change in one save', async () => {
    mockDevice({ config: { park: { ...mockConfig.park!, nina: { url: '', api: 'v2' } } } });
    withBox(box({ set: false, active: false, park: false, weather: false }));
    const bodies: Record<string, unknown>[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        bodies.push((await request.json()) as Record<string, unknown>);
        return HttpResponse.json({ success: true, revision: 1 });
      }),
    );
    await openPage();
    const weather = document.querySelector('[data-field="testBox.weather"]')!;
    fireEvent.click(weather.querySelector('input') ?? weather);
    expect(await screen.findByText('Saving also turns the SQMeter weather source off, in the same save.')).toBeInTheDocument();
    fireEvent.click(await screen.findByRole('button', { name: 'Turn on test box' }));
    await waitFor(() => expect(bodies).toHaveLength(1));
    expect(bodies[0].testBox).toMatchObject({ weather: true });
    expect(bodies[0].safety).toMatchObject({ http: { enabled: false } });
  });

  // Spec 024: the park bypass stands in for N.I.N.A., which park switches only don't use.
  it('blocks the park bypass with park switches only, with its own reason', async () => {
    mockDevice({ config: { park: { ...mockConfig.park!, source: 'switches', switches: 'two', nina: { url: '', api: 'v2' } } } });
    const park = await openPage();
    expect(park).toBeDisabled();
    expect(screen.getByText("Off with park switches only: there's no N.I.N.A. to bypass.")).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Park settings' })).toBeInTheDocument();
  });

  it('while on: until when, each check bypassed or checked, Turn off sends every bypass off, Extend sends only the hours', async () => {
    mockDevice({
      config: {
        park: { ...mockConfig.park!, nina: { url: '', api: 'v2' } },
        safety: { ...mockConfig.safety!, http: { enabled: false, url: '' }, mqtt: { enabled: false, topic: 'sqmeter' } },
        testBox: { park: true, weather: true, daylight: false, hours: 72, until: UNTIL },
      },
    });
    withBox(box());
    const bodies: unknown[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        bodies.push(await request.json());
        return HttpResponse.json({ success: true, revision: 1 });
      }),
    );
    await openPage();
    await waitFor(() => expect(document.querySelector('[data-test-box-mode="on"]')).not.toBeNull());
    expect(screen.getByText(formatUntil(UNTIL))).toBeInTheDocument();
    const row = (check: string) => document.querySelector(`[data-test-box-check="${check}"]`)!;
    expect(row('park')).toHaveTextContent('Bypassed');
    expect(row('weather')).toHaveTextContent('Bypassed');
    expect(row('daylight')).toHaveTextContent('Checked');
    fireEvent.click(screen.getByRole('button', { name: 'Extend by 72 h' }));
    await waitFor(() => expect(bodies).toEqual([{ testBox: { hours: 72 } }]));
    expect(await screen.findByText('Test box extended by 72 h.')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Turn off test box' }));
    await waitFor(() => expect(bodies[1]).toEqual({ testBox: { park: false, weather: false, daylight: false } }));
    expect(await screen.findByText('Test box off: every check is back.')).toBeInTheDocument();
  });

  it('shows May ask N.I.N.A. to park beside the bypasses (constitution III)', async () => {
    mockDevice();
    withBox(box());
    await openPage();
    const toggle = document.querySelector<HTMLInputElement>('[data-field="park.requestPark"] input, input[data-field="park.requestPark"]');
    expect(toggle).not.toBeNull();
    expect(toggle!.checked).toBe(true);
  });

  it('never says Off before the status is read, and keeps Turn off while the settings have a bypass', async () => {
    mockDevice({ config: { testBox: { park: true, weather: false, daylight: false, hours: 72, until: UNTIL } } });
    server.use(http.get('/api/status', () => HttpResponse.error()));
    await openPage();
    await waitFor(() => expect(document.querySelector('[data-test-box-mode="unknown"]')).not.toBeNull());
    expect(screen.queryByText('Off: every check applies.')).toBeNull();
    expect(screen.getByText('Unknown')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Turn off test box' })).toBeInTheDocument();
  });

  it('Extend waits for unsaved edits, and says why', async () => {
    mockDevice({
      config: {
        park: { ...mockConfig.park!, nina: { url: '', api: 'v2' } },
        testBox: { park: true, weather: false, daylight: false, hours: 72, until: UNTIL },
      },
    });
    withBox(box({ weather: false }));
    await openPage();
    const extend = await screen.findByRole('button', { name: 'Extend by 72 h' });
    expect(extend).toBeEnabled();
    fireEvent.click(document.querySelector('[data-field="testBox.daylight"] input, input[data-field="testBox.daylight"]')!);
    await waitFor(() => expect(extend).toBeDisabled());
    expect(screen.getByText('Save or discard your changes first.')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Turn off test box' })).toBeEnabled();
  });

  it('a refused Turn off says why and leaves the mode on', async () => {
    mockDevice({ config: { testBox: { park: true, weather: false, daylight: false, hours: 72, until: UNTIL } } });
    withBox(box());
    server.use(http.post('/api/config', () => HttpResponse.json({ success: false, error: 'Not now' }, { status: 400 })));
    await openPage();
    await waitFor(() => expect(document.querySelector('[data-test-box-mode="on"]')).not.toBeNull());
    fireEvent.click(screen.getByRole('button', { name: 'Turn off test box' }));
    expect(await screen.findByText(/Not now/)).toBeInTheDocument();
    expect(document.querySelector('[data-test-box-mode="on"]')).not.toBeNull();
  });

  it('while off: says so, and a switch turned on is saved with Turn on test box', async () => {
    mockDevice({ config: { park: { ...mockConfig.park!, nina: { url: '', api: 'v2' } } } });
    withBox(box({ set: false, active: false, park: false, weather: false }));
    const bodies: Record<string, unknown>[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        bodies.push((await request.json()) as Record<string, unknown>);
        return HttpResponse.json({ success: true, revision: 1 });
      }),
    );
    const park = await openPage();
    await waitFor(() => expect(document.querySelector('[data-test-box-mode="off"]')).not.toBeNull());
    expect(screen.getByText('Off: every check applies.')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Turn off test box' })).toBeNull();
    fireEvent.click(park.querySelector('input') ?? park);
    fireEvent.click(await screen.findByRole('button', { name: 'Turn on test box' }));
    await waitFor(() => expect(bodies[0]?.testBox).toMatchObject({ park: true }));
  });
});
