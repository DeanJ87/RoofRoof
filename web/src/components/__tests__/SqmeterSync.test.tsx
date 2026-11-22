import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { Config, SyncGroup, SyncStatus } from '../../types';

// Spec 020: a group that follows SQMeter is read-only in Settings, with "Edit
// here instead"; a group set here can follow again; a secret SQMeter has and
// RoofRoof lacks is asked for at its field.

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

  it('Time: read-only while following, with the age; Edit here instead unlocks it and Save sends follow false', async () => {
    mockDevice({ config: { sqmeterSync: following('time') } });
    liveSync({ readAgeSeconds: 180, time: groupStatus(true) });
    const saves = recordSaves();
    await openTab('time', 'Time');
    expect(await findField('ntp.timezone')).toBeDisabled();
    expect(field('ntp.server1')).toBeDisabled();
    expect(field('ntp.server2')).toBeDisabled();
    expect(field('ntp.syncIntervalMs')).toBeEnabled();
    expect((await screen.findAllByText('Follows SQMeter, checked 3 min ago')).length).toBeGreaterThan(0);
    fireEvent.click(screen.getAllByRole('button', { name: 'Edit here instead' })[0]);
    await waitFor(() => expect(field('ntp.server1')).toBeEnabled());
    expect(field('ntp.timezone')).toBeEnabled();
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].sqmeterSync!.time.follow).toBe(false);
    expect(saves[0].sqmeterSync!.mqtt.follow).toBe(false);
  });

  it('a group set here follows SQMeter again with its button, once an address is set', async () => {
    mockDevice();
    const saves = recordSaves();
    await openTab('time', 'Time');
    expect(await findField('ntp.server1')).toBeEnabled();
    fireEvent.click(screen.getAllByRole('button', { name: 'Follow SQMeter' })[0]);
    await waitFor(() => expect(field('ntp.server1')).toBeDisabled());
    expect(screen.getAllByText('Follows SQMeter once saved').length).toBeGreaterThan(0);
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].sqmeterSync!.time.follow).toBe(true);
  });

  it('offers no Follow SQMeter without its address, and nothing on older firmware', async () => {
    mockDevice({ config: { safety: { ...mockConfig.safety!, http: { enabled: false, url: '' } } } });
    await openTab('time', 'Time');
    await findField('ntp.server1');
    expect(screen.queryByRole('button', { name: 'Follow SQMeter' })).toBeNull();
  });

  it('Network: the broker, port and user name are read-only while following; the password asked for when needed', async () => {
    mockDevice({ config: { sqmeterSync: following('mqtt') } });
    liveSync({ problem: 'unreachable', readAgeSeconds: 900, mqtt: groupStatus(true, { needs: ['mqttPassword'] }) });
    await openTab('network', 'Network');
    expect(await findField('mqttBroker')).toBeDisabled();
    expect(field('mqttPort')).toBeDisabled();
    expect(field('mqtt.username')).toBeDisabled();
    expect(field('mqtt.password')).toBeEnabled();
    expect(field('mqttTopic')).toBeEnabled(); // RoofRoof's own base topic is never SQMeter's
    expect(await screen.findByText('Follows SQMeter: SQMeter not answering')).toBeInTheDocument();
    expect(screen.getByText("SQMeter's broker needs a password: enter it here.")).toBeInTheDocument();
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
    expect(await findField('safety.mqtt.topic')).toBeDisabled();
    expect(field('safety.http.url')).toBeEnabled();
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
    expect(await findField('alerts.ntfy.topic')).toBeDisabled();
    expect(field('alerts.ntfy.server')).toBeDisabled();
    expect(field('alerts.ntfy.token')).toBeEnabled();
    expect(await screen.findByText("SQMeter's ntfy server needs a token: enter it here.")).toBeInTheDocument();
    expect(screen.getByText('SQMeter uses Pushover: enter the user key and app token here.')).toBeInTheDocument();
    const note = screen.getByText('Follows SQMeter, checked 30 s ago');
    expect(within(note).getByRole('button', { name: 'Edit here instead' })).toBeInTheDocument();
  });
});
