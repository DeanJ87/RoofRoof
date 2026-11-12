import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest';
import { http, HttpResponse } from 'msw';
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Wizard from '../Wizard';
import { SCREENS } from '../wizard/screens';
import type { SystemStatus } from '../../types';
import { mockConfig, mockInputs, mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Specs 015 and 018: the wizard as a dialog of one-question screens; every
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
// The screen's question, once the settings are in (the loading screen has its own).
const question = async () => {
  await waitFor(() => expect(document.querySelector('[data-wizard-screen]')).not.toBeNull());
  return screen.getByRole('heading', { level: 2 });
};
const onScreen = (id: string) =>
  waitFor(() => expect(document.querySelector('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id));
const button = (name: string) => screen.getByRole('button', { name });
const PASSWORD_SET = { auth: { enabled: true, username: 'admin', password: '********' } };

describe('Wizard', () => {
  beforeEach(() => live());
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('is a modal dialog named by its question, with its progress', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    expect(await question()).toHaveTextContent('What should we call this roof?');
    const dialog = screen.getByRole('dialog', { name: 'What should we call this roof?' });
    expect(dialog).toHaveAttribute('aria-modal', 'true');
    expect(within(dialog).getByRole('progressbar', { name: 'Setup progress' })).toHaveAttribute('aria-valuetext', 'Screen 1 of 20');
    expect(within(dialog).getByRole('complementary')).toHaveTextContent('Why this matters');
    expect(screen.queryByRole('button', { name: 'Back' })).toBeNull(); // the first screen
  });

  it('opens at the first screen of the page saved last while setup is new', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 3 } } });
    render(<Wizard />);
    expect(await question()).toHaveTextContent('Is the Hall sensor fitted?');
  });

  it('opens at page 1 from the System page, and at one screen with ?screen=', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 6 } } });
    at('step=1');
    render(<Wizard />);
    await onScreen('name');
    cleanup();
    at('screen=parkSwitches');
    render(<Wizard />);
    expect(await question()).toHaveTextContent('How many park switches are fitted?');
  });

  it('on a new device a choice waits for an answer, with the reason', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 2 } } });
    at('screen=parkSwitches');
    render(<Wizard />);
    await question();
    const group = screen.getByRole('radiogroup', { name: 'How many park switches are fitted?' });
    expect(
      within(group)
        .getAllByRole('radio')
        .some((radio) => (radio as HTMLInputElement).checked),
    ).toBe(false);
    const go = button('Continue');
    expect(go).toBeDisabled();
    expect(document.getElementById(go.getAttribute('aria-describedby')!)).toHaveTextContent('Choose an answer to continue.');
    fireEvent.click(within(group).getByRole('radio', { name: 'One switch' }));
    expect(button('Continue')).toBeEnabled();
    expect(screen.getByText('Park switch 1')).toBeInTheDocument(); // its live state beside the choices
  });

  it('a device set up before shows its current settings as chosen', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    at('screen=parkSwitches');
    render(<Wizard />);
    await question();
    expect(screen.getByRole('radio', { name: 'None' })).toBeChecked();
    expect(button('Continue')).toBeEnabled();
  });

  it('starts with a password chosen when none is set, and warns without one', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    at('screen=password');
    render(<Wizard />);
    const on = await screen.findByRole('radio', { name: 'Set a password' });
    await waitFor(() => expect(on).toBeChecked());
    expect(button('Continue')).toBeDisabled(); // no password typed yet
    expect(screen.getByText('Enter a password, or choose No password.')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('radio', { name: 'No password' }));
    expect(await screen.findByText(/No password: anyone on your network can change settings and move the roof/)).toBeInTheDocument();
    expect(button('Continue')).toBeEnabled();
  });

  it('offers password protection once per visit, never again after it is turned off', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    at('screen=password');
    render(<Wizard />);
    await waitFor(() => expect(screen.getByRole('radio', { name: 'Set a password' })).toBeChecked());
    fireEvent.click(screen.getByRole('radio', { name: 'No password' }));
    fireEvent.click(button('Skip for now'));
    await onScreen('site');
    fireEvent.click(button('Back'));
    await onScreen('password');
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(screen.getByRole('radio', { name: 'Set a password' })).not.toBeChecked();
  });

  it('offers no roof command, no advanced policy setting and one question with few fields on any screen', async () => {
    const advanced = ['autoClose.', 'daylight.bandDeg', 'mqtt.changeMinSeconds', 'ui.confirmOpenClose', 'daylight.advanced'];
    for (const { id } of SCREENS) {
      mockDevice({
        config: {
          setup: { state: 'done', step: 0 },
          // One channel on, as the Channel screen leaves it.
          alerts: {
            ...mockConfig.alerts!,
            ntfy: { ...mockConfig.alerts!.ntfy, enabled: true },
            pushover: { ...mockConfig.alerts!.pushover, enabled: false },
          },
        },
      });
      at(`screen=${id}`);
      render(<Wizard />);
      await onScreen(id);
      for (const name of [/^open$/i, /^close$/i, /clear fault/i, /^stop/i, /^start calibration$/i])
        expect(screen.queryByRole('button', { name }), `${id}: ${name}`).toBeNull();
      for (const field of advanced) expect(document.querySelector(`[data-field^="${field}"]`), `${id}: ${field}`).toBeNull();
      // SC-001: one question, at most one choice group and at most four fields besides on/off switches.
      const panel = document.querySelector('.modal-panel')!;
      expect(panel.querySelectorAll('[role="radiogroup"]').length, id).toBeLessThanOrEqual(1);
      expect(panel.querySelectorAll('input:not([type="radio"]):not([role="switch"]), select').length, id).toBeLessThanOrEqual(4);
      expect(panel.querySelectorAll('h2').length, id).toBe(1);
      cleanup();
    }
  });

  it('links the N.I.N.A. docs anchor Settings uses', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    at('screen=alpaca');
    render(<Wizard />);
    const link = await screen.findByRole('link', { name: 'How N.I.N.A. opens and closes the roof' });
    expect(link.getAttribute('href')).toMatch(/docs\/user-guide\/alpaca\.md#nina-opens-and-closes-the-roof$/);
  });

  it('Continue saves the screen with the next screen’s page and moves on', async () => {
    mockDevice({ config: { ...PASSWORD_SET, setup: { state: 'new', step: 0 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('name');
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Backyard roof' } });
    fireEvent.click(button('Continue'));
    await onScreen('password');
    expect(saves).toHaveLength(1);
    expect(saves[0].deviceName).toBe('Backyard roof');
    expect(saves[0].setup).toEqual({ state: 'new', step: 0 }); // the password screen is still page 1
    // The saved edit stays: a later save sends it, not the value from before.
    fireEvent.click(button('Skip for now'));
    await onScreen('site');
    fireEvent.click(screen.getByRole('switch', { name: 'Location set' }));
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves).toHaveLength(2));
    expect(saves[1].deviceName).toBe('Backyard roof');
    expect(saves[1].setup).toEqual({ state: 'new', step: 1 });
  });

  it('keeps the screen and says why when the device refuses the save (the roof moving)', async () => {
    mockDevice({ config: { ...PASSWORD_SET, setup: { state: 'new', step: 0 } } });
    recordSaves(() => HttpResponse.json({ error: 'The roof is moving: wait until it stops' }, { status: 409 }));
    live({ ...mockStatus, roof: { ...mockStatus.roof!, state: 'opening' } });
    render(<Wizard />);
    await onScreen('name');
    expect(screen.getByText('The roof is moving: saves are refused until it stops')).toBeInTheDocument();
    // The Stop bar is inside the dialog, so it is in the focus loop and read by screen readers.
    const dialog = screen.getByRole('dialog');
    expect(within(dialog).getByRole('region', { name: 'Roof moving' })).toBeInTheDocument();
    expect(within(dialog).getByRole('button', { name: 'Stop roof' })).toBeEnabled();
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Backyard roof' } });
    fireEvent.click(button('Continue'));
    expect(await screen.findByText('The roof is moving: wait until it stops')).toBeInTheDocument();
    await onScreen('name');
  });

  it('an unchanged screen only records the step', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 1 } } });
    at('screen=time');
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('time');
    fireEvent.click(button('Continue'));
    await onScreen('supply');
    expect(saves).toEqual([{ setup: { state: 'new', step: 2 } }]);
  });

  it('leaving a screen drops its unsaved edits: a later save never carries them (FR-007)', async () => {
    mockDevice({
      config: {
        setup: { state: 'done', step: 0 },
        roof: { ...mockConfig.roof!, travelMm: 1850, speedMmPerSecond: 38, topDutyPercent: 100 },
      },
    });
    at('screen=motion');
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('motion');
    fireEvent.click(button('Use first-run values'));
    fireEvent.click(button('Skip for now'));
    await onScreen('weatherSource');
    fireEvent.click(screen.getByRole('radio', { name: 'Both' }));
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect((saves[0].roof as { topDutyPercent: number }).topDutyPercent).toBe(100);
    expect(saves[0].safety).toMatchObject({ http: { enabled: true }, mqtt: { enabled: true } });
  });

  it('Skip for now and Back never save', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('name');
    fireEvent.click(button('Skip for now'));
    await onScreen('password');
    fireEvent.click(button('Back'));
    await onScreen('name');
    expect(saves).toHaveLength(0);
  });

  it('a screen change moves focus to its question', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    await onScreen('name');
    fireEvent.click(button('Skip for now'));
    await waitFor(() => expect(document.activeElement).toHaveTextContent('Protect changes with a password?'));
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuetext', 'Screen 2 of 20');
  });

  it('Test on the weather address screen shows the verdict and each source', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 6 } } });
    at('screen=weatherAddress');
    render(<Wizard />);
    await onScreen('weatherAddress');
    fireEvent.click(button('Test'));
    const result = await screen.findByText('Weather verdict', {}, { timeout: 3000 });
    expect(result.closest('[data-test-result]')).toHaveTextContent('Safe');
    expect(result.closest('[data-test-result]')).toHaveTextContent('Answering');
  });

  it('Test on the N.I.N.A. screen waits for the saved address, then shows the mount', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 7 } } });
    const saves = recordSaves();
    let generation = 1;
    server.use(http.get('/api/roof/inputs', () => HttpResponse.json({ ...mockInputs, nina: { ...mockInputs.nina, generation } })));
    render(<Wizard />);
    await onScreen('nina');
    fireEvent.input(screen.getByLabelText('N.I.N.A. address', { selector: 'input' }), { target: { value: 'http://10.0.0.5:1888' } });
    fireEvent.click(button('Save and test'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(screen.getByText("Waiting for the device's next reading.")).toBeInTheDocument();
    generation = 2; // the device has started again with the new address
    const row = await screen.findByText('Mount connected', {}, { timeout: 4000 });
    expect(row.closest('[data-test-result]')).toHaveTextContent('At park');
    expect(saves[0].setup).toBeUndefined(); // a Test doesn't move the wizard on
  });

  it('Escape and Close ask before leaving; Keep going returns; nothing is saved', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 2 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('supply');
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    expect(await question()).toHaveTextContent('Leave setup?');
    fireEvent.click(button('Keep going'));
    await onScreen('supply');
    fireEvent.click(button('Close setup'));
    expect(await question()).toHaveTextContent('Leave setup?');
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' }); // Escape on "Leave setup?" keeps going
    await onScreen('supply');
    expect(saves).toHaveLength(0);
  });

  it('warns before skipping setup, and only a confirmed skip sets it done', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 2 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('supply');
    fireEvent.click(button('Close setup'));
    fireEvent.click(await screen.findByRole('button', { name: 'Skip setup' }));
    expect(screen.getByText(/the roof may not close itself in bad weather or tell anyone/)).toBeInTheDocument();
    expect(saves).toHaveLength(0);
    fireEvent.click(button('Skip setup anyway'));
    await waitFor(() => expect(saves).toEqual([{ setup: { state: 'done', step: 0 } }]));
  });

  it('the review lists every check; Finish sets setup done and links commissioning', async () => {
    mockDevice({ config: { setup: { state: 'offered', step: 10 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('review');
    expect(document.querySelectorAll('[data-check]')).toHaveLength(13);
    expect(screen.queryByRole('button', { name: 'Skip for now' })).toBeNull();
    fireEvent.click(button('Finish'));
    expect(await screen.findByText(/Setup finished/)).toBeInTheDocument();
    expect(await question()).toHaveTextContent('Your roof is set up.');
    expect(saves.at(-1)?.setup).toEqual({ state: 'done', step: 0 });
    expect(screen.getByRole('link', { name: 'Commissioning guide' })).toHaveAttribute(
      'href',
      expect.stringContaining('docs/hardware/commissioning.md'),
    );
    expect(screen.getByRole('link', { name: 'Gate for unattended use' })).toHaveAttribute('href', expect.stringContaining('#stage-11'));
    expect(button('Go to dashboard')).toBeInTheDocument();
  });

  it('the review’s Change opens the page’s first screen', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    at('screen=review');
    render(<Wizard />);
    await onScreen('review');
    fireEvent.click(within(document.querySelector('[data-check="hall"]') as HTMLElement).getByRole('button', { name: 'Change' }));
    await onScreen('hallFitted');
  });

  it('Hall sensor not fitted explains, links the bench test and leaves out the thresholds', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 3 } } });
    render(<Wizard />);
    await onScreen('hallFitted');
    fireEvent.click(screen.getByRole('radio', { name: 'Not fitted yet' }));
    expect(screen.getByText(/the automatic close can't work/)).toBeInTheDocument();
    expect(screen.getByRole('link', { name: 'Bench test without the sensor' })).toHaveAttribute(
      'href',
      expect.stringContaining('quick-bench-test.md'),
    );
    fireEvent.click(button('Continue'));
    await onScreen('calibrate');
  });

  it('first-run values set the top duty to 40% while the motion is the factory default', async () => {
    mockDevice({
      config: {
        setup: { state: 'new', step: 5 },
        roof: { ...mockConfig.roof!, travelMm: 1850, speedMmPerSecond: 38, topDutyPercent: 100 },
      },
    });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('motion');
    fireEvent.click(button('Use first-run values'));
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect((saves[0].roof as { topDutyPercent: number }).topDutyPercent).toBe(40);
  });

  // Spec 016: the wizard offers the calibration run but never moves the roof itself.
  it('the calibration run screen links the attended page and has no motion control', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 4 } } });
    render(<Wizard />);
    await onScreen('calibrate');
    expect(button('Open the calibration page')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /^(Start calibration|Open|Close|Stop calibration)$/ })).toBeNull();
  });

  it('one alert channel is chosen, and its details follow', async () => {
    mockDevice({
      config: {
        setup: { state: 'new', step: 8 },
        alerts: { ...mockConfig.alerts!, enabled: false, pushover: { ...mockConfig.alerts!.pushover, enabled: false } },
      },
    });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('alertChannel');
    expect(screen.getByText('No alert channel: critical alerts will reach no one.')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('radio', { name: 'ntfy' }));
    fireEvent.click(screen.getByRole('radio', { name: 'Pushover' })); // a change of mind turns ntfy back off
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].alerts).toMatchObject({
      enabled: true,
      pushover: { enabled: true },
      ntfy: { enabled: false },
      webhook: { enabled: false },
    });
    await onScreen('alertDetails');
    expect(screen.getByText('User key')).toBeInTheDocument();
  });

  it('with a channel already saved, the chosen card follows the pick', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } }); // Pushover saved on
    at('screen=alertChannel');
    render(<Wizard />);
    await onScreen('alertChannel');
    expect(screen.getByRole('radio', { name: 'Pushover' })).toBeChecked();
    fireEvent.click(screen.getByRole('radio', { name: 'ntfy' }));
    expect(screen.getByRole('radio', { name: 'ntfy' })).toBeChecked();
    fireEvent.click(screen.getByRole('radio', { name: 'Webhook' }));
    expect(screen.getByRole('radio', { name: 'Webhook' })).toBeChecked();
  });

  it('Skip for now picks the next screen from the settings as saved', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 8 }, alerts: { ...mockConfig.alerts!, enabled: false } } });
    render(<Wizard />);
    await onScreen('alertChannel');
    fireEvent.click(screen.getByRole('radio', { name: 'ntfy' }));
    fireEvent.click(button('Skip for now'));
    await onScreen('daylight'); // alerts stay off, so their details are left out
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
});
