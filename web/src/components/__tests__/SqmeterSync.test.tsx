import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { Config, SyncGroup, SyncStatus } from '../../types';

// Spec 020: a group that follows SQMeter says so in Settings; spec 026: its
// fields stay editable and typing in one sets the group here ("Set here, not
// from SQMeter", Follow SQMeter again), never silently overwritten later; a
// secret SQMeter has and RoofRoof lacks is asked for at its field.

const following = (...groups: SyncGroup[]) => {
  const sync = structuredClone(mockConfig.sqmeterSync!);
  for (const group of groups) sync[group] = { follow: true, syncedAt: 1763540000 };
  return sync;
};
const liveSync = (over: Partial<SyncStatus>) =>
  server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, sync: { ...mockStatus.sync!, ...over } })));
const openTab = async (tab: string, name: string) => {
  window.history.replaceState(null, '', `/settings?tab=${tab}`);
  render(<Toaster />);
  render(<Settings />);
  expect(await screen.findByRole('tab', { name, selected: true })).toBeInTheDocument();
};
const field = (path: string) => document.querySelector<HTMLInputElement>(`[data-field="${path}"]`);
const findField = (path: string) =>
  waitFor(() => {
    const el = field(path);
    if (!el) throw new Error(`no ${path} yet`);
    return el;
  });
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
const groupStatus = (follow: boolean, extra: Partial<SyncStatus['time']> = {}) => ({
  follow,
  syncedAt: follow ? 1763540000 : null,
  refused: null,
  needs: [],
  ...extra,
});

describe('Following SQMeter', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('Time: following, with the age; typing a time zone sets the group here, and Save sends follow false with it', async () => {
    mockDevice({ config: { sqmeterSync: following('time') } });
    liveSync({ readAgeSeconds: 180, time: groupStatus(true) });
    const saves = recordSaves();
    await openTab('time', 'Time');
    expect(await findField('ntp.timezone')).toBeEnabled();
    expect((await screen.findAllByText('Follows SQMeter, checked 3 min ago')).length).toBeGreaterThan(0);
    fireEvent.input(field('ntp.server1')!, { target: { value: 'time.example' } });
    expect((await screen.findAllByText('Set here, not from SQMeter')).length).toBeGreaterThan(0);
    expect(screen.getAllByRole('button', { name: 'Follow SQMeter again' }).length).toBeGreaterThan(0);
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].ntp.server1).toBe('time.example');
    expect(saves[0].sqmeterSync).toEqual({ time: { follow: false } });
  });

  it('a group set here follows SQMeter again with its button, once an address is set', async () => {
    mockDevice();
    const saves = recordSaves();
    await openTab('time', 'Time');
    expect(await findField('ntp.server1')).toBeEnabled();
    fireEvent.click(screen.getAllByRole('button', { name: 'Follow SQMeter again' })[0]);
    expect((await screen.findAllByText('Follows SQMeter once saved')).length).toBeGreaterThan(0);
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].sqmeterSync!.time.follow).toBe(true);
  });

  it('offers no Follow SQMeter without its address, and nothing on older firmware', async () => {
    mockDevice({ config: { safety: { ...mockConfig.safety!, http: { enabled: false, url: '' } } } });
    await openTab('time', 'Time');
    await findField('ntp.server1');
    expect(screen.queryByRole('button', { name: 'Follow SQMeter again' })).toBeNull();
  });

  // The owner's case: a login typed while the MQTT group followed SQMeter is saved as typed, and the group is set here.
  it('Network: typing a user name and password sets the MQTT group here; the save says so', async () => {
    mockDevice({ config: { sqmeterSync: following('mqtt') } });
    liveSync({ problem: 'unreachable', readAgeSeconds: 900, mqtt: groupStatus(true, { needs: ['mqttPassword'] }) });
    const saves = recordSaves();
    await openTab('network', 'Network');
    expect(await findField('mqttBroker')).toBeEnabled();
    expect(await screen.findByText('Follows SQMeter: SQMeter not answering')).toBeInTheDocument();
    expect(screen.getByText("SQMeter's broker needs a password: enter it here.")).toBeInTheDocument();
    fireEvent.input(field('mqtt.username')!, { target: { value: 'me' } });
    fireEvent.input(field('mqtt.password')!, { target: { value: 'mine' } });
    expect(await screen.findByText('Set here, not from SQMeter')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].mqtt).toMatchObject({ username: 'me', password: 'mine' });
    expect(saves[0].sqmeterSync).toEqual({ mqtt: { follow: false } });
  });

  it("shows the device's reason when it held or refused SQMeter's value", async () => {
    mockDevice({ config: { sqmeterSync: following('mqtt') } });
    liveSync({ mqtt: groupStatus(true, { refused: 'MQTT roof commands are on: set the broker here' }) });
    await openTab('network', 'Network');
    expect(await screen.findByText('MQTT roof commands are on: set the broker here')).toBeInTheDocument();
  });

  it('Roof: the MQTT weather topic follows with the MQTT group', async () => {
    mockDevice({
      config: { sqmeterSync: following('mqtt'), safety: { ...mockConfig.safety!, mqtt: { enabled: true, topic: 'sqmeter' } } },
    });
    await openTab('roof', 'Roof');
    expect(await findField('safety.mqtt.topic')).toBeEnabled();
    expect(screen.getAllByText(/^Follows SQMeter/).length).toBeGreaterThan(0);
    fireEvent.input(field('safety.mqtt.topic')!, { target: { value: 'obs/sqm' } });
    expect((await screen.findAllByText('Set here, not from SQMeter')).length).toBeGreaterThan(0);
  });

  it('Alerts: the ntfy server and topic follow; the token is asked for when needed', async () => {
    mockDevice({
      config: {
        sqmeterSync: following('alerts'),
        alerts: { ...mockConfig.alerts!, ntfy: { enabled: true, server: 'https://ntfy.sh', topic: 'darkskylab-sqm', token: '' } },
      },
    });
    liveSync({ readAgeSeconds: 30, alerts: groupStatus(true, { needs: ['ntfyToken', 'pushoverKeys'] }) });
    await openTab('alerts', 'Alerts');
    expect(await findField('alerts.ntfy.topic')).toBeEnabled();
    expect(field('alerts.ntfy.token')).toBeEnabled();
    expect(await screen.findByText("SQMeter's ntfy server needs a token: enter it here.")).toBeInTheDocument();
    expect(screen.getByText('SQMeter uses Pushover: enter the user key and app token here.')).toBeInTheDocument();
    const note = screen.getByText(/^Follows SQMeter, checked 30 s ago/);
    expect(within(note).getByRole('button', { name: 'More information' })).toBeInTheDocument(); // typing sets it here: "?"
    expect(within(note).queryByRole('button', { name: 'Edit here instead' })).toBeNull();
  });

  // Spec 024: the sync keeps the passwords and switches too, and says why it couldn't read the passwords.
  it("Network: a following MQTT group says why the sync couldn't read SQMeter's passwords", async () => {
    mockDevice({ config: { sqmeterSync: following('mqtt') } });
    liveSync({ readAgeSeconds: 30, secrets: 'loginRefused', mqtt: groupStatus(true) });
    await openTab('network', 'Network');
    expect(
      await screen.findByText("SQMeter refused the login: check SQMeter's user name and password in Settings, Roof."),
    ).toBeInTheDocument();
    expect(screen.queryByText(/The sync also keeps SQMeter's passwords/)).toBeNull(); // not while it can't
  });

  it('Network: no password note once received, nor for the time group', async () => {
    mockDevice({ config: { sqmeterSync: following('mqtt', 'time') } });
    liveSync({ readAgeSeconds: 30, secrets: 'received', mqtt: groupStatus(true), time: groupStatus(true) });
    await openTab('network', 'Network');
    expect(await screen.findByText(/The sync also keeps SQMeter's passwords/)).toBeInTheDocument();
    expect(screen.queryByText(/SQMeter refused the login/)).toBeNull();
  });
});
