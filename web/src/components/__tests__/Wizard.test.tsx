import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest';
import { http, HttpResponse } from 'msw';
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Router from 'preact-router';
import Wizard from '../Wizard';
import { SCREENS } from '../wizard/screens';
import type { SystemStatus } from '../../types';
import { mockConfig, mockInputs, mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Specs 015, 018 and 025: the wizard as a dialog of one-question screens in the
// installer's order; every spec 015 guarantee (saves through the settings API
// with the part, refused saves, discards on leaving a screen, Test freshness,
// skip, finish); progress that survives a reload in every setup state; alerts
// as a multiple choice with their fields inline.

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
const NO_PASSWORD = { auth: { enabled: false, username: 'admin', password: '' } };
const MOTOR = ['direction', 'teach', 'calibrate'];

describe('Wizard', () => {
  beforeEach(() => live());
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('is a modal dialog named by its question, with its progress', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    expect(await question()).toHaveTextContent('What should we call this roof?');
    const dialog = screen.getByRole('dialog', { name: 'What should we call this roof?' });
    expect(dialog).toHaveAttribute('aria-modal', 'true');
    // SC-002: the mock DevKit with its site typed in: 15 screens (no network question, no manual values).
    expect(within(dialog).getByRole('progressbar', { name: 'Setup progress' })).toHaveAttribute('aria-valuetext', 'Screen 1 of 15');
    expect(within(dialog).getByRole('complementary')).toHaveTextContent('Why this matters');
    expect(screen.queryByRole('button', { name: 'Back' })).toBeNull(); // the first screen
  });

  it('opens at the first screen of the part saved last', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 4 } } });
    render(<Wizard />);
    expect(await question()).toHaveTextContent('Where is SQMeter?');
  });

  // FR-003: a re-run from System (setup done) that was left part-way resumes, not at the start.
  it('resumes at the saved part in every setup state', async () => {
    for (const state of ['new', 'offered', 'done'] as const) {
      mockDevice({ config: { setup: { state, step: 9 } } });
      render(<Wizard />);
      await onScreen('alerts');
      cleanup();
    }
  });

  it('applies an explicit start once: ?step=1 and ?screen= open there, then the address resumes', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 6 } } });
    at('step=1');
    render(
      <Router>
        <Wizard path="/wizard" />
      </Router>,
    );
    await onScreen('name');
    await waitFor(() => expect(window.location.search).toBe(''));
    cleanup();
    render(<Wizard />); // a reload: no start in the address any more
    await onScreen('direction');
    cleanup();
    at('screen=parkSwitches');
    render(<Wizard />);
    expect(await question()).toHaveTextContent('How does RoofRoof know the mount is parked?');
  });

  it('on a new device a choice waits for an answer, with the reason', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 5 } } });
    render(<Wizard />);
    await onScreen('parkSwitches');
    const group = screen.getByRole('radiogroup', { name: 'How does RoofRoof know the mount is parked?' });
    expect(
      within(group)
        .getAllByRole('radio')
        .some((radio) => (radio as HTMLInputElement).checked),
    ).toBe(false);
    const go = button('Continue');
    expect(go).toBeDisabled();
    expect(document.getElementById(go.getAttribute('aria-describedby')!)).toHaveTextContent('Choose an answer to continue.');
    fireEvent.click(within(group).getByRole('radio', { name: /^N.I.N.A. and park switches/ }));
    expect(button('Continue')).toBeEnabled();
  });

  it('a device set up before shows its current settings as chosen', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    at('screen=parkSwitches');
    render(<Wizard />);
    await question();
    expect(screen.getByRole('radio', { name: /^N.I.N.A. only/ })).toBeChecked();
    expect(button('Continue')).toBeEnabled();
  });

  // The reported bug (spec 025 R1): the screen used to turn protection back on when it opened.
  it('never changes the password setting when its screen opens; "No password" is kept across visits', async () => {
    mockDevice({ config: { ...NO_PASSWORD, setup: { state: 'new', step: 10 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('password');
    expect(screen.getByRole('radio', { name: 'Set a password' })).not.toBeChecked();
    expect(screen.getByRole('radio', { name: 'No password' })).not.toBeChecked();
    expect(button('Continue')).toBeDisabled();
    fireEvent.click(screen.getByRole('radio', { name: 'No password' }));
    expect(screen.getByText(/No password: anyone on your network can change settings and move the roof/)).toBeInTheDocument();
    fireEvent.click(button('Continue'));
    await onScreen('review');
    expect(saves).toEqual([{ setup: { state: 'new', step: 11 } }]); // protection was off: nothing else to save
    cleanup();

    // A reload (the device now resumes at the review), then Back to the password screen.
    mockDevice({ config: { ...NO_PASSWORD, setup: { state: 'new', step: 11 } } });
    render(<Wizard />);
    await onScreen('review');
    fireEvent.click(button('Back'));
    await onScreen('password');
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(screen.getByRole('radio', { name: 'No password' })).toBeChecked();
    expect(screen.getByRole('radio', { name: 'Set a password' })).not.toBeChecked();
    expect(button('Continue')).toBeEnabled();
    cleanup();

    // Opened straight at the password screen (a link), it still shows what was saved.
    at('screen=password');
    render(<Wizard />);
    await onScreen('password');
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(screen.getByRole('radio', { name: 'No password' })).toBeChecked();
  });

  it('"Set a password" waits for one', async () => {
    mockDevice({ config: { ...NO_PASSWORD, setup: { state: 'new', step: 10 } } });
    render(<Wizard />);
    await onScreen('password');
    fireEvent.click(screen.getByRole('radio', { name: /Set a password/ }));
    expect(button('Continue')).toBeDisabled();
    expect(screen.getByText('Enter a password, or choose No password.')).toBeInTheDocument();
  });

  it('only the motor screens offer a move, each behind "I am at the roof"; no screen has Open, Close or Clear fault', async () => {
    const advanced = ['autoClose.', 'daylight.bandDeg', 'mqtt.changeMinSeconds', 'ui.confirmOpenClose', 'daylight.advanced'];
    for (const { id } of SCREENS) {
      mockDevice({ config: { setup: { state: 'done', step: 0 } } });
      at(`screen=${id}`);
      render(<Wizard />);
      await onScreen(id);
      for (const name of [/^open$/i, /^close$/i, /clear fault/i])
        expect(screen.queryByRole('button', { name }), `${id}: ${name}`).toBeNull();
      for (const field of advanced) expect(document.querySelector(`[data-field^="${field}"]`), `${id}: ${field}`).toBeNull();
      const moves = screen.queryAllByRole('button', { name: /^(Hold to open|Hold to close|Start calibration)$/ });
      if (MOTOR.includes(id)) {
        expect(moves.length, id).toBeGreaterThan(0);
        for (const move of moves) expect(move, id).toBeDisabled(); // nobody said they are at the roof
      } else expect(moves, id).toHaveLength(0);
      expect(document.querySelectorAll('.modal-panel h2').length, id).toBe(1);
      expect(document.querySelectorAll('.modal-panel [role="radiogroup"], .modal-panel [role="group"]').length, id).toBeLessThanOrEqual(1);
      cleanup();
    }
  });

  it('links the N.I.N.A. docs anchor Settings uses; Alpaca is a switch on the N.I.N.A. screen', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    at('screen=nina');
    render(<Wizard />);
    const link = await screen.findByRole('link', { name: 'How N.I.N.A. opens and closes the roof' });
    expect(link.getAttribute('href')).toMatch(/docs\/user-guide\/alpaca\.md#nina-opens-and-closes-the-roof$/);
    fireEvent.click(screen.getByRole('switch', { name: /Open and close over Alpaca/ }));
    expect(screen.getByText("Alpaca is off: N.I.N.A. can't open or close the roof.")).toBeInTheDocument();
  });

  it('Continue saves the screen with the next screen’s part and moves on', async () => {
    mockDevice({ config: { ...PASSWORD_SET, setup: { state: 'new', step: 0 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('name');
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Backyard roof' } });
    fireEvent.click(button('Continue'));
    await onScreen('board');
    expect(saves).toHaveLength(1);
    expect(saves[0].deviceName).toBe('Backyard roof');
    expect(saves[0].setup).toEqual({ state: 'new', step: 1 });
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

  it('an unchanged screen only records the part', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 4 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('sqmeter');
    fireEvent.click(button('Continue'));
    await onScreen('site');
    expect(saves).toEqual([{ setup: { state: 'new', step: 4 } }]);
  });

  // FR-002: a reload after Skip for now opens the next part, not the skipped one.
  it('Skip for now records the next part and saves nothing else; Back saves nothing', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('name');
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Dropped' } });
    fireEvent.click(button('Skip for now'));
    await onScreen('board');
    await waitFor(() => expect(saves).toEqual([{ setup: { state: 'new', step: 1 } }]));
    fireEvent.click(button('Back'));
    await onScreen('name');
    expect(saves).toHaveLength(1);
  });

  it('leaving a screen drops its unsaved edits: a later save never carries them', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    at('screen=parkSwitches');
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('parkSwitches');
    fireEvent.click(screen.getByRole('radio', { name: /^Park switches only/ }));
    fireEvent.click(button('Skip for now'));
    await onScreen('nina');
    fireEvent.input(screen.getByLabelText('N.I.N.A. address', { selector: 'input' }), { target: { value: 'http://10.0.0.9:1888' } });
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves.some((save) => save.park)).toBe(true));
    const save = saves.find((body) => body.park)!;
    expect(save.park).toEqual({ nina: { url: 'http://10.0.0.9:1888' } }); // never the skipped park source
  });

  it('a screen change moves focus to its question', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    await onScreen('name');
    fireEvent.click(button('Skip for now'));
    await waitFor(() => expect(document.activeElement).toHaveTextContent('Which board is RoofRoof on?'));
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuetext', 'Screen 2 of 15');
  });

  it('Test on the N.I.N.A. screen waits for the saved address, then shows the mount', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 5 } } });
    at('screen=nina');
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

  // Spec 022 on the test box branch: the bypasses set, SQMeter only for site data, N.I.N.A. skipped.
  it('a test box sets the bypasses, keeps SQMeter for site data and skips N.I.N.A.', async () => {
    const bench = {
      park: { ...mockConfig.park!, nina: { url: '', api: 'v2' } },
      safety: { ...mockConfig.safety!, http: { ...mockConfig.safety!.http, enabled: false }, mqtt: { enabled: false, topic: 'sqmeter' } },
    };
    mockDevice({ config: { ...bench, setup: { state: 'new', step: 3 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('testBox');
    fireEvent.click(screen.getByRole('radio', { name: /No telescope yet \(bench setup\)/ }));
    expect(screen.getByText(/Never with a telescope/)).toBeInTheDocument();
    fireEvent.click(button('Continue'));
    await onScreen('sqmeter');
    expect(saves[0]).toMatchObject({ testBox: { park: true, weather: true, daylight: true }, setup: { state: 'new', step: 4 } });
    cleanup();
    mockDevice({
      config: {
        ...bench,
        testBox: { ...mockConfig.testBox!, park: true, weather: true, daylight: true },
        setup: { state: 'new', step: 4 },
      },
    });
    render(<Wizard />);
    await onScreen('sqmeter');
    expect(screen.getByText('Test box: the weather check is bypassed, so SQMeter supplies only site data.')).toBeInTheDocument();
    expect(screen.queryByRole('switch', { name: /Weather from SQMeter/ })).toBeNull();
    expect(screen.getByLabelText("SQMeter's address", { selector: 'input' })).toBeInTheDocument();
    cleanup();
    mockDevice({
      config: {
        ...bench,
        testBox: { ...mockConfig.testBox!, park: true, weather: true, daylight: true },
        setup: { state: 'new', step: 5 },
      },
    });
    render(<Wizard />);
    await onScreen('parkSwitches');
    fireEvent.click(screen.getByRole('radio', { name: /^N.I.N.A. only/ }));
    fireEvent.click(button('Continue'));
    await onScreen('direction'); // no N.I.N.A. screen
  });

  it('owner decision 2026-11-24: a test box clears the N.I.N.A. address and keeps the weather check where SQMeter is on', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 3 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('testBox');
    fireEvent.click(screen.getByRole('radio', { name: /No telescope yet \(bench setup\)/ }));
    expect(screen.getByText('This also removes the N.I.N.A. address, in the same save.')).toBeInTheDocument();
    expect(screen.getByText('A weather source is on, so the weather check stays on.')).toBeInTheDocument();
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves[0]).toMatchObject({ testBox: { park: true, daylight: true }, park: { nina: { url: '' } } }));
  });

  // FR-017, FR-018: SQMeter once; what it answers shows as done with Edit.
  it('asks SQMeter once and shows the site and time it supplies as done, with Edit', async () => {
    const following = {
      location: { ...mockConfig.location!, source: 'sqmeter' },
      sqmeterSync: { ...mockConfig.sqmeterSync!, time: { follow: true, syncedAt: 1 } },
      setup: { state: 'new', step: 4 },
    };
    mockDevice({ config: following });
    render(<Wizard />);
    await onScreen('sqmeter');
    expect(screen.getAllByLabelText("SQMeter's address", { selector: 'input' })).toHaveLength(1);
    expect(document.getElementById('sqmeter-done-location')).toHaveTextContent('The location comes from SQMeter.');
    expect(document.getElementById('sqmeter-done-time')).toHaveTextContent('The time zone and time servers come from SQMeter.');
    expect(screen.getByRole('progressbar').getAttribute('aria-valuetext')).toBe('Screen 5 of 14'); // no site screen
    fireEvent.click(within(document.getElementById('sqmeter-done-time') as HTMLElement).getByRole('button', { name: 'Edit' }));
    await onScreen('site');
    fireEvent.click(button('Continue'));
    await onScreen('daylight');
  });

  it('Escape and Close ask before leaving; Keep going returns; nothing is saved', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 1 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('board');
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    expect(await question()).toHaveTextContent('Leave setup?');
    fireEvent.click(button('Keep going'));
    await onScreen('board');
    fireEvent.click(button('Close setup'));
    expect(await question()).toHaveTextContent('Leave setup?');
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' }); // Escape on "Leave setup?" keeps going
    await onScreen('board');
    expect(saves).toHaveLength(0);
  });

  it('warns before skipping setup, and only a confirmed skip sets it done', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 1 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('board');
    fireEvent.click(button('Close setup'));
    fireEvent.click(await screen.findByRole('button', { name: 'Skip setup' }));
    expect(screen.getByText(/the roof may not close itself in bad weather or tell anyone/)).toBeInTheDocument();
    expect(saves).toHaveLength(0);
    fireEvent.click(button('Skip setup anyway'));
    await waitFor(() => expect(saves).toEqual([{ setup: { state: 'done', step: 0 } }]));
  });

  it('the review lists every check; Finish sets setup done and links commissioning', async () => {
    mockDevice({ config: { setup: { state: 'offered', step: 11 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('review');
    expect(document.querySelectorAll('[data-check]')).toHaveLength(14);
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

  it('the review’s Change opens the part that sets it', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    at('screen=review');
    render(<Wizard />);
    await onScreen('review');
    fireEvent.click(within(document.querySelector('[data-check="hall"]') as HTMLElement).getByRole('button', { name: 'Change' }));
    await onScreen('teach');
  });

  it('first-run values set the top duty to 40% while the motion is the factory default', async () => {
    mockDevice({
      config: {
        setup: { state: 'new', step: 8 },
        roof: { ...mockConfig.roof!, travelMm: 1850, speedMmPerSecond: 38, topDutyPercent: 100 },
      },
    });
    at('screen=motion');
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('motion');
    fireEvent.click(button('Use first-run values'));
    fireEvent.click(button('Continue'));
    await onScreen('alerts');
    expect((saves[0].roof as { topDutyPercent: number }).topDutyPercent).toBe(40);
    expect(saves[0].setup).toEqual({ state: 'new', step: 9 });
  });
});
