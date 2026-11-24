import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import Wizard from '../Wizard';
import type { SystemStatus } from '../../types';
import { mockConfig, mockSqmeterSite, mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 018, continued from Wizard.test.tsx: the review rounds' cases. The wizard as a dialog of one-question screens; every
// spec 015 guarantee (saves through the settings API with the step, refused
// saves, discards on leaving a screen, Test freshness, skip, finish, no roof
// command on any screen) on the new screens.

const live = (status: SystemStatus = mockStatus) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

// Every POST /api/config body, in order.
const recordSaves = (reply: () => Response = () => HttpResponse.json({ success: true })) => {
  const bodies: Record<string, unknown>[] = [];
  server.use(
    http.post('/api/config', async ({ request }) => {
      bodies.push((await request.json()) as Record<string, unknown>);
      return reply();
    }),
  );
  return bodies;
};

const at = (query: string) => window.history.replaceState(null, '', `/wizard?${query}`);
const onScreen = (id: string) =>
  waitFor(() => expect(document.querySelector('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id));
const button = (name: string) => screen.getByRole('button', { name });
const PASSWORD_SET = { auth: { enabled: true, username: 'admin', password: '********' } };

describe('Wizard: choices, skips and refusals', () => {
  beforeEach(() => live());
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('with a channel already saved, its box is ticked and more can be ticked beside it', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } }); // Pushover saved on
    at('screen=alerts');
    render(<Wizard />);
    await onScreen('alerts');
    expect(screen.getByRole('group')).toBeInTheDocument();
    expect(screen.getByRole('checkbox', { name: /Pushover/ })).toBeChecked();
    fireEvent.click(screen.getByRole('checkbox', { name: /ntfy/ }));
    expect(screen.getByRole('checkbox', { name: /ntfy/ })).toBeChecked();
    expect(screen.getByRole('checkbox', { name: /Pushover/ })).toBeChecked();
    // The pick survives the screen being rebuilt (Escape, then Keep going).
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    fireEvent.click(await screen.findByRole('button', { name: 'Keep going' }));
    await onScreen('alerts');
    expect(screen.getByRole('checkbox', { name: /ntfy/ })).toBeChecked();
    // "No alerts for now" is an answer of its own, with its warning.
    fireEvent.click(screen.getByRole('checkbox', { name: /No alerts for now/ }));
    expect(screen.getByText('No alert channel: critical alerts will reach no one.')).toBeInTheDocument();
  });

  it('Skip for now picks the next screen from the settings as saved', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 9 }, alerts: { ...mockConfig.alerts!, enabled: false } } });
    recordSaves();
    render(<Wizard />);
    await onScreen('alerts');
    fireEvent.click(screen.getByRole('checkbox', { name: /ntfy/ }));
    fireEvent.click(button('Skip for now'));
    await onScreen('password');
  });

  it('a refused save moves focus to its reason, which is announced', async () => {
    mockDevice({ config: { ...PASSWORD_SET, setup: { state: 'new', step: 0 } } });
    recordSaves(() => HttpResponse.json({ error: 'The roof is moving: wait until it stops' }, { status: 409 }));
    render(<Wizard />);
    await onScreen('name');
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Backyard roof' } });
    fireEvent.click(button('Continue'));
    const alert = await screen.findByRole('alert');
    expect(alert).toHaveTextContent('The roof is moving: wait until it stops');
    await waitFor(() => expect(document.activeElement).toBe(alert));
  });

  it('without a location, the daylight screen says where it is set and goes there, keeping the answer', async () => {
    mockDevice({
      config: {
        setup: { state: 'new', step: 4 },
        location: { ...mockConfig.location!, set: false, source: 'manual' },
        safety: { ...mockConfig.safety!, http: { enabled: false, url: '' } },
      },
    });
    const saves = recordSaves();
    at('screen=daylight');
    render(<Wizard />);
    await onScreen('daylight');
    fireEvent.click(screen.getByRole('radio', { name: 'Alert and close' }));
    expect(screen.getByText('Daylight needs your location, which is set on the Site screen earlier in setup.')).toBeInTheDocument();
    fireEvent.click(button('Set location'));
    await onScreen('site');
    expect(saves).toHaveLength(1);
    expect(saves[0].daylight).toMatchObject({ mode: 'close' });
    expect(saves[0].setup).toEqual({ state: 'new', step: 4 });
  });

  // Spec 020 and 025: SQMeter's address first, and the copy fills later screens.
  it('the SQMeter screen copies its settings; the copy fills the Site screen and Continue saves it', async () => {
    mockDevice({ config: { ...PASSWORD_SET, setup: { state: 'new', step: 4 } } });
    const saves = recordSaves();
    let copied: unknown = null;
    server.use(
      http.post('/api/sqmeter/site', async ({ request }) => {
        copied = await request.json();
        return HttpResponse.json(mockSqmeterSite);
      }),
    );
    render(<Wizard />);
    await onScreen('sqmeter');
    expect(screen.getByText('Where is SQMeter?')).toBeInTheDocument();
    expect(screen.getByRole('switch', { name: 'Weather from SQMeter' })).toBeInTheDocument();
    expect(button('Test')).toBeInTheDocument();
    const address = screen.getByLabelText("SQMeter's address", { selector: 'input' });
    fireEvent.input(address, { target: { value: 'http://10.0.0.5/api/safety' } });
    fireEvent.click(button('Copy from SQMeter'));
    await screen.findByRole('switch', { name: 'Time zone' });
    expect(copied).toEqual({ url: 'http://10.0.0.5/api/safety', username: '', password: '' });
    fireEvent.click(button('Apply ticked'));
    expect(await screen.findByText('Applied: Continue saves them.')).toBeInTheDocument();
    // Spec 024: the address was saved before the copy (SQMeter shares its passwords only with the saved one).
    expect(saves).toHaveLength(1);
    expect((saves[0].safety as { http: { url: string } }).http.url).toBe('http://10.0.0.5/api/safety');
    expect(saves[0].ntp).toBeUndefined();
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves).toHaveLength(2));
    expect(saves[1].ntp).toMatchObject({ timezone: 'GMT0BST,M3.5.0/1,M10.5.0', server2: 'time.nist.gov' });
    expect(saves[1].setup).toEqual({ state: 'new', step: 4 });
  });

  // Spec 024: SQMeter's MQTT password arrives with the copy; the field says so and MQTT's Test waits for the save.
  it("the copy brings SQMeter's MQTT password and switch into the SQMeter screen's MQTT fields", async () => {
    mockDevice({
      config: {
        ...PASSWORD_SET,
        setup: { state: 'new', step: 4 },
        mqtt: { ...mockConfig.mqtt, enabled: false, broker: '', password: '' },
        safety: {
          ...mockConfig.safety!,
          http: { enabled: true, url: 'http://sqmeter.local/api/safety' },
          mqtt: { ...mockConfig.safety!.mqtt, enabled: true },
        },
      },
    });
    const saves = recordSaves();
    server.use(
      http.post('/api/sqmeter/site', () =>
        HttpResponse.json({
          ...mockSqmeterSite,
          mqtt: { ...mockSqmeterSite.mqtt!, enabled: true },
          secrets: { problem: null, http: true, mqttPassword: 'received', ntfyToken: 'none', pushover: 'none' },
        }),
      ),
    );
    at('screen=sqmeter');
    render(<Wizard />);
    await onScreen('sqmeter');
    fireEvent.click(button('Copy from SQMeter'));
    fireEvent.click(await screen.findByRole('button', { name: 'Apply ticked' }));
    expect(await screen.findByText('Received from SQMeter; never shown here.')).toBeInTheDocument();
    expect(screen.getByLabelText('Broker', { selector: 'input' })).toHaveValue('192.168.1.10');
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves.some((body) => body.mqtt)).toBe(true));
    expect(saves.find((body) => body.mqtt)!.mqtt).toMatchObject({ enabled: true, broker: '192.168.1.10', password: '(from SQMeter)' });
  });

  it('lets Continue go on from the SQMeter screen with no address', async () => {
    mockDevice({
      config: { ...PASSWORD_SET, setup: { state: 'new', step: 4 }, safety: { ...mockConfig.safety!, http: { enabled: false, url: '' } } },
    });
    recordSaves();
    render(<Wizard />);
    await onScreen('sqmeter');
    expect(button('Copy from SQMeter')).toBeDisabled();
    expect(button('Continue')).toBeEnabled();
    fireEvent.click(button('Continue'));
    await onScreen('site');
  });

  it('with the location and time from SQMeter, the screen says so with Edit, and the Site screen is left out', async () => {
    const sqmeterSync = { ...mockConfig.sqmeterSync!, time: { follow: true, syncedAt: 1763540000 } };
    mockDevice({
      config: {
        ...PASSWORD_SET,
        setup: { state: 'new', step: 4 },
        sqmeterSync,
        location: { ...mockConfig.location!, source: 'sqmeter' },
        safety: { ...mockConfig.safety!, http: { enabled: true, url: 'http://sqmeter.local/api/safety' } },
      },
    });
    recordSaves();
    at('screen=sqmeter');
    render(<Wizard />);
    await onScreen('sqmeter');
    expect(screen.getByText('The location comes from SQMeter.')).toBeInTheDocument();
    expect(screen.getByText('The time zone and time servers come from SQMeter.')).toBeInTheDocument();
    expect(screen.getAllByRole('button', { name: 'Edit' })).toHaveLength(2);
    fireEvent.click(button('Continue'));
    await onScreen('daylight'); // the Site screen isn't asked
    fireEvent.click(button('Back'));
    await onScreen('sqmeter');
    fireEvent.click(screen.getAllByRole('button', { name: 'Edit' })[0]);
    await onScreen('site');
  });

  // Spec 024: how the park is known, then (a switch source only) how many switches.
  it('asks the park source, then the switch count with its readings and warnings, and saves both', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 5 }, testBox: { ...mockConfig.testBox!, park: true } } });
    const saves = recordSaves();
    at('screen=parkSwitches');
    render(<Wizard />);
    await onScreen('parkSwitches');
    fireEvent.click(screen.getByRole('radio', { name: /^Park switches only/ }));
    fireEvent.click(button('Continue'));
    await onScreen('parkSwitchCount');
    expect(screen.getByRole('dialog', { name: 'How many park switches are fitted?' })).toBeInTheDocument();
    expect(button('Continue')).toBeDisabled(); // a fact only the owner knows
    fireEvent.click(screen.getByRole('radio', { name: /^One switch/ }));
    expect(screen.getByText('Park switch 1')).toBeInTheDocument();
    expect(document.querySelector('[data-park-note="oneSwitch"]')).toHaveTextContent(/fit two on separate cables/);
    expect(document.querySelector('[data-park-note="wiring"]')).toHaveTextContent(/3 roof openings/);
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves.length).toBeGreaterThanOrEqual(1));
    const park = saves.map((body) => body.park).find(Boolean) as Record<string, unknown>;
    expect(park).toMatchObject({ source: 'switches', switches: 'one' });
    // The test box's park bypass stands in for N.I.N.A., which switches only don't use.
    expect(saves.map((body) => body.testBox).find(Boolean)).toMatchObject({ park: false });
    // With the bypass gone N.I.N.A. is asked, its address optional.
    await onScreen('nina');
    expect(screen.getByText(/Address optional with switches only/)).toBeInTheDocument();
  });

  it('N.I.N.A. only skips the switch count', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 5 }, park: { ...mockConfig.park!, source: 'ninaAndSwitches', switches: 'two' } } });
    const saves = recordSaves();
    at('screen=parkSwitches');
    render(<Wizard />);
    await onScreen('parkSwitches');
    fireEvent.click(screen.getByRole('radio', { name: /^N.I.N.A. only/ }));
    fireEvent.click(button('Continue'));
    await onScreen('nina');
    expect(saves.map((body) => body.park).find(Boolean)).toMatchObject({ source: 'nina', switches: 'none' });
  });

  it('with park switches only the N.I.N.A. address is optional', async () => {
    mockDevice({
      config: {
        setup: { state: 'done', step: 0 },
        park: { ...mockConfig.park!, source: 'switches', switches: 'two', nina: { url: '', api: 'v2' } },
      },
    });
    at('screen=nina');
    render(<Wizard />);
    await onScreen('nina');
    expect(screen.getByText(/Address optional with switches only/)).toBeInTheDocument();
    expect(screen.queryByText(/No address: the park can't be confirmed/)).toBeNull();
  });
});
