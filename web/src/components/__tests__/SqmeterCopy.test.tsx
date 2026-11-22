import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig, mockSqmeterSite, mockStatus } from '../../mocks/data';
import { fetchSqmeterSite } from '../../lib/sqmeterCopy';
import type { Config, LocationStatus } from '../../types';

// Settings → Roof: Copy from SQMeter and the location's source (spec 020 FR-016, FR-019).

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
const card = async () => (await screen.findByRole('heading', { name: 'Site from SQMeter' })).closest('section') as HTMLElement;
const recordSaves = () => {
  const saves: Config[] = [];
  server.use(
    http.post('/api/config', async ({ request }) => {
      saves.push((await request.json()) as Config);
      return HttpResponse.json({ success: true });
    }),
  );
  return saves;
};
const withLocation = (location: LocationStatus) =>
  server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, location })));

describe('Copy from SQMeter', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('lists what differs, applies only the ticked rows to the form and saves only through Save', async () => {
    mockDevice();
    const saves = recordSaves();
    await openRoofTab();
    const site = await card();
    fireEvent.click(within(site).getByRole('button', { name: 'Copy from SQMeter' }));
    expect(await within(site).findByRole('switch', { name: 'Time zone' })).toBeChecked();
    for (const name of ["Follow SQMeter's location", 'Time servers', 'MQTT broker', 'ntfy topic'])
      expect(within(site).getByRole('switch', { name })).toBeChecked();
    // SQMeter's base topic already matches the weather source's.
    expect(within(site).queryByRole('switch', { name: 'Weather topic' })).toBeNull();
    expect(within(site).getByText('SQMeter: GMT0BST,M3.5.0/1,M10.5.0; RoofRoof: GMT0')).toBeInTheDocument();
    expect(within(site).getByText('Enter the MQTT password in Settings, Network: it is never copied.')).toBeInTheDocument();
    expect(within(site).getByText(/SQMeter uses Pushover/)).toBeInTheDocument();
    expect(within(site).getByText('Copied settings keep following SQMeter: Edit here instead stops it.')).toBeInTheDocument();
    for (const name of ["Follow SQMeter's location", 'Time servers', 'MQTT broker', 'ntfy topic'])
      fireEvent.click(within(site).getByRole('switch', { name }));
    expect(within(site).queryByText(/Enter the MQTT password/)).toBeNull();
    fireEvent.click(within(site).getByRole('button', { name: 'Apply ticked' }));
    expect(await within(site).findByText('Applied: save to keep them.')).toBeInTheDocument();
    expect(saves).toHaveLength(0);
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    // Spec 021: only what the copy changed is sent.
    expect(saves[0].ntp).toEqual({ timezone: 'GMT0BST,M3.5.0/1,M10.5.0' });
    expect(saves[0].mqtt).toBeUndefined();
    expect(saves[0].location).toBeUndefined();
    // Spec 020: the copied time group keeps following SQMeter; the others stay set here.
    expect(saves[0].sqmeterSync).toEqual({ time: { follow: true } });
  });

  it('clears the MQTT password when the broker is applied', async () => {
    mockDevice();
    const saves = recordSaves();
    await openRoofTab();
    const site = await card();
    fireEvent.click(within(site).getByRole('button', { name: 'Copy from SQMeter' }));
    await within(site).findByRole('switch', { name: 'MQTT broker' });
    fireEvent.click(within(site).getByRole('button', { name: 'Apply ticked' }));
    fireEvent.click(await screen.findByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].mqtt).toEqual(
      expect.objectContaining({ broker: '192.168.1.10', username: 'observatory', password: null }),
    );
    expect(saves[0].mqtt.enabled).toBeUndefined();
    expect(saves[0].alerts!.ntfy).toEqual({ topic: 'my-observatory' }); // never switched on by a copy
    expect(saves[0].location).toEqual(expect.objectContaining({ source: 'sqmeter', set: true, latitude: 51.4769 }));
  });

  it("shows the device's reason when the copy fails", async () => {
    mockDevice();
    server.use(
      http.post('/api/sqmeter/site', () => HttpResponse.json({ error: 'SQMeter not answering: connection refused' }, { status: 502 })),
    );
    await openRoofTab();
    const site = await card();
    fireEvent.click(within(site).getByRole('button', { name: 'Copy from SQMeter' }));
    expect(await within(site).findByText('SQMeter not answering: connection refused')).toBeInTheDocument();
  });

  it('says so when there is nothing to copy', async () => {
    mockDevice();
    server.use(
      http.post('/api/sqmeter/site', () =>
        HttpResponse.json({ ...mockSqmeterSite, location: null, time: null, mqtt: null, ntfy: null, pushover: null }),
      ),
    );
    await openRoofTab();
    const site = await card();
    fireEvent.click(within(site).getByRole('button', { name: 'Copy from SQMeter' }));
    expect(await within(site).findByText('Nothing to copy: RoofRoof already matches SQMeter.')).toBeInTheDocument();
  });

  it("is unavailable, with the reason, without SQMeter's address; the address shows here while HTTP is off", async () => {
    mockDevice({ config: { safety: { ...mockConfig.safety!, http: { enabled: false, url: '' } } } });
    await openRoofTab();
    const site = await card();
    expect(within(site).getByRole('button', { name: 'Copy from SQMeter' })).toBeDisabled();
    expect(within(site).getByText("SQMeter's address isn't set: enter it to copy from SQMeter.")).toBeInTheDocument();
    fireEvent.input(within(site).getByRole('textbox'), { target: { value: 'http://10.0.0.5/api/safety' } });
    expect(within(site).getByRole('button', { name: 'Copy from SQMeter' })).toBeEnabled();
  });
});

describe('Location source', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('from SQMeter: the kept location read-only, where it came from and its age', async () => {
    mockDevice({
      config: { location: { source: 'sqmeter', set: true, latitude: 51.4769, longitude: -0.0005, sqmeter: { gps: true, savedAt: 0 } } },
    });
    withLocation({ source: 'sqmeter', set: true, gps: true, savedAt: 1763540000, readAgeSeconds: 180, problem: null });
    await openRoofTab();
    expect(await findField('location.source')).toHaveValue('sqmeter');
    expect(field('location.latitude')).toBeNull();
    expect(field('location.set')).toBeNull();
    expect(await screen.findByText('51.4769 N, 0.0005 W')).toBeInTheDocument();
    expect(await screen.findByText("Location from SQMeter's GPS, read 3 min ago")).toBeInTheDocument();
  });

  it("from SQMeter: the read's problem", async () => {
    mockDevice({ config: { location: { source: 'sqmeter', set: false, latitude: 0, longitude: 0 } } });
    withLocation({ source: 'sqmeter', set: false, gps: false, savedAt: null, readAgeSeconds: null, problem: 'unreachable' });
    await openRoofTab();
    expect(await screen.findByText('SQMeter not answering')).toBeInTheDocument();
    expect(screen.getByText('Not read yet')).toBeInTheDocument();
  });

  it('from SQMeter without its address: a warning that goes to the address', async () => {
    mockDevice({
      config: {
        location: { source: 'sqmeter', set: false, latitude: 0, longitude: 0 },
        safety: { ...mockConfig.safety!, http: { enabled: false, url: '' } },
      },
    });
    await openRoofTab();
    expect(await screen.findByText("SQMeter's address isn't set: set it to follow its location.")).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Set address' })).toBeInTheDocument();
  });

  it('switches between SQMeter and typed in, and saves the source', async () => {
    mockDevice();
    const saves = recordSaves();
    await openRoofTab();
    expect(await findField('location.source')).toHaveValue('manual');
    expect(field('location.set')).not.toBeNull();
    fireEvent.change(field('location.source')!, { target: { value: 'sqmeter' } });
    await waitFor(() => expect(field('location.set')).toBeNull());
    expect(screen.getByText("Save to follow SQMeter's location: the device reads it then.")).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].location!.source).toBe('sqmeter');
  });
});

describe('fetchSqmeterSite', () => {
  it('sends a typed address and answers the site', async () => {
    let body: unknown = null;
    server.use(
      http.post('/api/sqmeter/site', async ({ request }) => {
        body = await request.json();
        return HttpResponse.json(mockSqmeterSite);
      }),
    );
    expect(await fetchSqmeterSite('http://10.0.0.5/api/safety')).toEqual({ ok: true, site: mockSqmeterSite });
    expect(body).toEqual({ url: 'http://10.0.0.5/api/safety' });
  });

  it("answers the device's reason when it refuses", async () => {
    server.use(http.post('/api/sqmeter/site', () => HttpResponse.json({ error: 'SQMeter answered HTTP 404' }, { status: 502 })));
    expect(await fetchSqmeterSite()).toEqual({ ok: false, error: 'SQMeter answered HTTP 404' });
  });
});
