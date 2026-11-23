import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import Wizard from '../Wizard';
import type { SystemStatus } from '../../types';
import { mockCalibration, mockStatus, mockTeach } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 025: the motor screens (direction, teach, calibrate) inside the wizard's
// dialog. Every gate of specs 016 and 019 stays: the person confirms they are at
// the roof, the checks apply (or a test box bypass is named), and the device
// checks again when a move starts.

// A roof that passes every check: parked, safe weather, no fault, resting at the closed end.
const ready = (): SystemStatus => {
  const s = structuredClone(mockStatus);
  s.roof = { ...s.roof!, weather: 'safe', parkConfirmed: true, fault: null, closePhase: 'idle', supplyLow: false, state: 'closed' };
  s.roof.hall = { counts: 700, level: 'low' };
  return s;
};

const live = (status: SystemStatus = ready()) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

const at = (query: string) => window.history.replaceState(null, '', `/wizard?${query}`);
const onScreen = (id: string) =>
  waitFor(() => expect(document.querySelector('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id));
const button = (name: string) => screen.getByRole('button', { name });
const holdOpen = () => screen.findByRole('button', { name: 'Hold to open' });
const NOT_ATTENDED = 'Confirm that you are at the roof first';

describe('Wizard: the motor screens', () => {
  beforeEach(() => {
    live();
    mockDevice({ config: { setup: { state: 'new', step: 6 } } });
    server.use(
      http.get('/api/roof/teach', () => HttpResponse.json(mockTeach)),
      http.get('/api/roof/calibration', () =>
        HttpResponse.json({ ...mockCalibration, state: 'idle', ending: null, proposal: null, legs: [], request: null }),
      ),
    );
  });
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('direction: the hold buttons wait for "I am at the roof, watching it", with the checks listed', async () => {
    at('screen=direction');
    render(<Wizard />);
    await onScreen('direction');
    const open = await holdOpen();
    const close = button('Hold to close');
    expect(open).toBeDisabled();
    expect(close).toBeDisabled();
    expect(screen.getByText(NOT_ATTENDED)).toBeInTheDocument();
    expect(screen.getByText('Mount parked')).toBeInTheDocument(); // the checks list
    fireEvent.click(screen.getByRole('switch', { name: 'I am at the roof, watching it' }));
    await waitFor(() => expect(open).toBeEnabled());
    expect(close).toBeEnabled();
    expect(screen.queryByText(NOT_ATTENDED)).toBeNull();
  });

  it('a roof that is not parked blocks the jog and names the check', async () => {
    const s = ready();
    s.roof!.parkConfirmed = false;
    s.roof!.parkText = 'not parked: N.I.N.A. not responding';
    live(s);
    at('screen=direction');
    render(<Wizard />);
    await onScreen('direction');
    fireEvent.click(await screen.findByRole('switch', { name: 'I am at the roof, watching it' }));
    expect(await holdOpen()).toBeDisabled();
    expect(screen.getByText('Not ready: Mount parked')).toBeInTheDocument();
  });

  it('a test box names the checks it bypasses', async () => {
    const s = ready();
    s.roof!.testBox = { ...s.roof!.testBox!, set: true, active: true, park: true, weather: true, until: 2000000000 };
    live(s);
    at('screen=direction');
    render(<Wizard />);
    await onScreen('direction');
    await holdOpen();
    const note = screen.getByText(/^Test box:/);
    expect(note).toHaveTextContent('park check (no telescope)');
    expect(note).toHaveTextContent('weather check (no weather source)');
    expect(note).not.toHaveTextContent('daylight close');
    expect(note).toHaveTextContent('faults, low supply and time limits still stop the roof');
  });

  it('without a test box, no bypass is mentioned', async () => {
    at('screen=teach');
    render(<Wizard />);
    await onScreen('teach');
    await screen.findByRole('button', { name: 'Hold to open' });
    expect(screen.queryByText(/^Test box:/)).toBeNull();
  });

  it('direction: "No, it closed" says to swap the wires and holds Continue; "Yes, it opened" lets it go', async () => {
    at('screen=direction');
    render(<Wizard />);
    await onScreen('direction');
    await holdOpen();
    expect(button('Continue')).toBeDisabled(); // a choice, not yet made
    fireEvent.click(screen.getByRole('radio', { name: /^No, it closed/ }));
    expect(screen.getByText("Wrong way: swap the motor's two wires at the driver, then try again.")).toBeInTheDocument();
    expect(button('Continue')).toBeDisabled();
    fireEvent.click(screen.getByRole('radio', { name: /^Yes, it opened/ }));
    expect(screen.queryByText(/Wrong way: swap/)).toBeNull();
    expect(button('Continue')).toBeEnabled();
  });

  it('teach: the hold buttons and marks are the Teach card’s, behind the same confirmation', async () => {
    at('screen=teach');
    render(<Wizard />);
    await onScreen('teach');
    const open = await holdOpen();
    expect(open).toBeDisabled();
    expect(screen.getByText(NOT_ATTENDED)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'This is closed' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('switch', { name: 'I am at the roof, watching it' }));
    await waitFor(() => expect(open).toBeEnabled());
    expect(button('Continue')).toBeEnabled(); // teaching is optional
  });

  it('"Enter manually instead" opens the motion screen with the Hall fields, travel, speed and top duty', async () => {
    at('screen=direction');
    render(<Wizard />);
    await onScreen('direction');
    fireEvent.click(await screen.findByRole('button', { name: 'Enter manually instead' }));
    await onScreen('motion');
    for (const field of [
      'roof.hallHighEnd',
      'roof.hallHighCounts',
      'roof.hallLowCounts',
      'roof.travelMm',
      'roof.speedMmPerSecond',
      'roof.topDutyPercent',
    ])
      expect(document.querySelector(`[data-field="${field}"]`), field).not.toBeNull();
    expect(screen.queryByRole('button', { name: 'Hold to open' })).toBeNull();
  });

  it('calibrate: Start calibration waits for "I am at the roof and can see it"', async () => {
    at('screen=calibrate');
    render(<Wizard />);
    await onScreen('calibrate');
    const start = await screen.findByRole('button', { name: 'Start calibration' });
    expect(start).toBeDisabled();
    expect(screen.getByText(NOT_ATTENDED)).toBeInTheDocument();
    fireEvent.click(screen.getByRole('switch', { name: 'I am at the roof and can see it' }));
    await waitFor(() => expect(start).toBeEnabled());
    expect(screen.queryByText(NOT_ATTENDED)).toBeNull();
  });

  it('calibrate: Start sends the attended run to the device', async () => {
    let body: unknown = null;
    server.use(
      http.post('/api/roof/calibration/start', async ({ request }) => {
        body = await request.json();
        return HttpResponse.json({ success: true, state: 'running' });
      }),
    );
    at('screen=calibrate');
    render(<Wizard />);
    await onScreen('calibrate');
    fireEvent.click(await screen.findByRole('switch', { name: 'I am at the roof and can see it' }));
    await waitFor(() => expect(button('Start calibration')).toBeEnabled());
    fireEvent.click(button('Start calibration'));
    await waitFor(() => expect(body).toMatchObject({ attended: true }));
  });
});
