import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig, mockSqmeterSite, mockStatus } from '../../mocks/data';
import type { Config, SyncStatus } from '../../types';

// Spec 020: SQMeter's login next to its address, and every value copied from
// SQMeter lands as an ordinary setting on its own Settings page, visible (also
// with MQTT or the channel off), marked "Follows SQMeter" and editable with
// "Edit here instead"; a secret to enter is asked for at its field.

const field = (path: string) => document.querySelector<HTMLInputElement>(`[data-field="${path}"]`);
const findField = (path: string) =>
  waitFor(() => {
    const el = field(path);
    if (!el) throw new Error(`no ${path} yet`);
    return el;
  });
const open = async (tab: string, name: string) => {
  window.history.replaceState(null, '', `/settings?tab=${tab}`);
  render(<Toaster />);
  render(<Settings />);
  expect(await screen.findByRole('tab', { name, selected: true })).toBeInTheDocument();
};
const toTab = async (name: string) => {
  fireEvent.click(screen.getByRole('tab', { name }));
  expect(await screen.findByRole('tab', { name, selected: true })).toBeInTheDocument();
};
const card = async (title: string) => (await screen.findByRole('heading', { name: title })).closest('section') as HTMLElement;
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
// Everything off and set here: MQTT, ntfy and the MQTT weather source.
const offDevice = () =>
  mockDevice({
    config: {
      mqtt: { ...mockConfig.mqtt, enabled: false, broker: '', username: '' },
      safety: { ...mockConfig.safety!, mqtt: { enabled: false, topic: 'sqmeter' } },
    },
  });
const copyAll = async () => {
  const site = await card('Site from SQMeter');
  fireEvent.click(within(site).getByRole('button', { name: 'Copy from SQMeter' }));
  await within(site).findByRole('switch', { name: 'Time zone' });
  fireEvent.click(within(site).getByRole('button', { name: 'Apply ticked' }));
  await within(site).findByText('Applied: save to keep them.');
};

describe("SQMeter's login", () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('sits next to the address in the weather card and the Site card, and is saved', async () => {
    mockDevice();
    const saves = recordSaves();
    await open('roof', 'Roof');
    expect(await findField('safety.http.username')).toBeInTheDocument();
    expect(field('safety.http.password')).toHaveAttribute('type', 'password');
    fireEvent.input(field('safety.http.username')!, { target: { value: 'admin' } });
    fireEvent.input(field('safety.http.password')!, { target: { value: 'sqmeter' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].safety!.http).toMatchObject({ username: 'admin', password: 'sqmeter' });
  });

  it('shows in the Site card while the HTTP weather source is off', async () => {
    mockDevice({
      config: { safety: { ...mockConfig.safety!, http: { enabled: false, url: 'http://sqmeter.local', username: '', password: '' } } },
    });
    await open('roof', 'Roof');
    const site = await card('Site from SQMeter');
    expect(within(site).getByRole('textbox', { name: 'SQMeter user name' })).toBeInTheDocument();
  });

  it("refuses a user name with ':'", async () => {
    mockDevice();
    await open('roof', 'Roof');
    fireEvent.input(await findField('safety.http.username'), { target: { value: 'a:b' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(await screen.findByText("SQMeter login: the user name can't contain ':'")).toBeInTheDocument();
  });

  it('the copy sends the login as typed, and says to check the password when SQMeter wants one', async () => {
    mockDevice();
    const bodies: unknown[] = [];
    server.use(
      http.post('/api/sqmeter/site', async ({ request }) => {
        bodies.push(await request.json());
        return HttpResponse.json({ error: 'SQMeter wants a password' }, { status: 502 });
      }),
    );
    await open('roof', 'Roof');
    fireEvent.input(await findField('safety.http.username'), { target: { value: 'admin' } });
    fireEvent.input(field('safety.http.password')!, { target: { value: 'sqmeter' } });
    const site = await card('Site from SQMeter');
    fireEvent.click(within(site).getByRole('button', { name: 'Copy from SQMeter' }));
    const alert = await within(site).findByRole('alert');
    expect(alert).toHaveTextContent('SQMeter wants a password');
    expect(alert).toHaveTextContent("Enter or check SQMeter's password in the fields above, then copy again.");
    expect(bodies[0]).toEqual({ url: mockConfig.safety!.http.url, username: 'admin', password: 'sqmeter' });
  });
});

describe('Copied values on their own Settings pages', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('MQTT off: the copied broker, port and user show on Network, marked and editable, with the password asked for', async () => {
    offDevice();
    await open('roof', 'Roof');
    await copyAll();
    await toTab('Network');
    const broker = await findField('mqttBroker');
    expect(broker).toHaveValue('192.168.1.10');
    expect(field('mqtt.username')).toHaveValue('observatory');
    expect(broker).toBeDisabled();
    expect(screen.getByText('Follows SQMeter once saved')).toBeInTheDocument();
    expect(field('mqtt.password')).toBeEnabled();
    // SQMeter's broker has a password: it is asked for at the field until one is typed.
    expect(screen.getByText("SQMeter's broker needs a password: enter it here.")).toBeInTheDocument();
    fireEvent.input(field('mqtt.password')!, { target: { value: 'secret' } });
    expect(screen.queryByText("SQMeter's broker needs a password: enter it here.")).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Edit here instead' }));
    await waitFor(() => expect(field('mqttBroker')).toBeEnabled());
  });

  it("a new broker without a password: RoofRoof's old one is cleared, and not said to be SQMeter's", async () => {
    mockDevice({ config: { mqtt: { ...mockConfig.mqtt, enabled: false, broker: '10.9.9.9', username: '', password: '********' } } });
    server.use(
      http.post('/api/sqmeter/site', () =>
        HttpResponse.json({ ...mockSqmeterSite, mqtt: { ...mockSqmeterSite.mqtt!, passwordSet: false } }),
      ),
    );
    await open('roof', 'Roof');
    await copyAll();
    await toTab('Network');
    expect(await screen.findByText('Password cleared for the new broker: enter it if it needs one.')).toBeInTheDocument();
    expect(screen.queryByText("SQMeter's broker needs a password: enter it here.")).toBeNull();
  });

  it('Time: the copied time zone and servers show on Time, marked', async () => {
    offDevice();
    await open('roof', 'Roof');
    await copyAll();
    await toTab('Time');
    expect(await findField('ntp.timezone')).toHaveValue('GMT0BST,M3.5.0/1,M10.5.0');
    expect(field('ntp.server2')).toHaveValue('time.nist.gov');
    expect(screen.getAllByRole('button', { name: 'Edit here instead' }).length).toBeGreaterThan(0);
  });

  it('ntfy off: the copied server and topic show on Alerts, marked', async () => {
    offDevice();
    await open('roof', 'Roof');
    await copyAll();
    await toTab('Alerts');
    expect(await findField('alerts.ntfy.topic')).toHaveValue('my-observatory');
    expect(field('alerts.ntfy.server')).toHaveValue('https://ntfy.sh');
    expect(field('alerts.ntfy.topic')).toBeDisabled();
    expect(field('alerts.ntfy.token')).toBeEnabled();
  });

  it('the MQTT weather source off: a copied weather topic shows on Roof, marked', async () => {
    offDevice();
    server.use(
      http.post('/api/sqmeter/site', () =>
        HttpResponse.json({ ...mockSqmeterSite, mqtt: { ...mockSqmeterSite.mqtt!, topic: 'observatory/sqm' } }),
      ),
    );
    await open('roof', 'Roof');
    expect(field('safety.mqtt.topic')).toBeNull();
    await copyAll();
    expect(await findField('safety.mqtt.topic')).toHaveValue('observatory/sqm');
    expect(field('safety.mqtt.topic')).toBeDisabled();
  });

  it('the copied location follows SQMeter on the Daylight card', async () => {
    offDevice();
    await open('roof', 'Roof');
    await copyAll();
    expect(await findField('location.source')).toHaveValue('sqmeter');
  });

  it('Pushover off: a copy from an SQMeter with Pushover keys shows its fields on Alerts and asks for them', async () => {
    mockDevice({
      config: { alerts: { ...mockConfig.alerts!, pushover: { enabled: false, userKey: '', appToken: '', sound: '' } } },
    });
    await open('roof', 'Roof');
    await copyAll();
    await toTab('Alerts');
    expect(await findField('alerts.pushover.userKey')).toBeEnabled();
    expect(screen.getByText('SQMeter uses Pushover: enter the user key and app token here.')).toBeInTheDocument();
  });

  it('following SQMeter alone (a new device, no address) shows no channel that is off', async () => {
    mockDevice({
      config: {
        mqtt: { ...mockConfig.mqtt, enabled: false, broker: '', username: '' },
        safety: {
          ...mockConfig.safety!,
          http: { enabled: false, url: '', username: '', password: '' },
          mqtt: { enabled: false, topic: 'sqmeter' },
        },
        alerts: {
          ...mockConfig.alerts!,
          ntfy: { ...mockConfig.alerts!.ntfy, enabled: false, topic: '' },
          pushover: { ...mockConfig.alerts!.pushover, enabled: false },
        },
        sqmeterSync: { time: { follow: true, syncedAt: 0 }, mqtt: { follow: true, syncedAt: 0 }, alerts: { follow: true, syncedAt: 0 } },
      },
    });
    await open('roof', 'Roof');
    expect(field('safety.mqtt.topic')).toBeNull();
    await toTab('Network');
    expect(field('mqttBroker')).toBeNull();
    await toTab('Alerts');
    expect(field('alerts.ntfy.topic')).toBeNull();
    expect(field('alerts.pushover.userKey')).toBeNull();
  });

  it('a secret SQMeter has and RoofRoof lacks is asked for at its field, also with MQTT, ntfy and Pushover off', async () => {
    mockDevice({
      config: {
        mqtt: { ...mockConfig.mqtt, enabled: false, broker: '' },
        alerts: { ...mockConfig.alerts!, pushover: { ...mockConfig.alerts!.pushover, enabled: false } },
      },
    });
    const sync: SyncStatus = {
      ...mockStatus.sync!,
      mqtt: { follow: true, syncedAt: 1, refused: null, needs: ['mqttPassword'] },
      alerts: { follow: true, syncedAt: 1, refused: null, needs: ['ntfyToken', 'pushoverKeys'] },
    };
    server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, sync })));
    await open('network', 'Network');
    expect(await findField('mqtt.password')).toBeEnabled();
    expect(await screen.findByText("SQMeter's broker needs a password: enter it here.")).toBeInTheDocument();
    await toTab('Alerts');
    expect(await findField('alerts.ntfy.token')).toBeEnabled();
    expect(await screen.findByText("SQMeter's ntfy server needs a token: enter it here.")).toBeInTheDocument();
    expect(field('alerts.pushover.userKey')).not.toBeNull();
    expect(screen.getByText('SQMeter uses Pushover: enter the user key and app token here.')).toBeInTheDocument();
  });
});
