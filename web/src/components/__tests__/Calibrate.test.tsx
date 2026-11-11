import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Calibrate from '../Calibrate';
import type { CalibrationStatus, SystemStatus } from '../../types';
import { mockCalibration, mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 016: the attended calibration page. Start needs every check and the
// confirmation; Stop is shown, enabled, for the whole run; the proposal is
// shown beside the saved values and saved only on Accept.

const ready = (): SystemStatus => {
  const s = structuredClone(mockStatus);
  s.roof = { ...s.roof!, weather: 'safe', parkConfirmed: true, fault: null, closePhase: 'idle', supplyLow: false, state: 'closed' };
  s.roof.hall = { counts: 700, level: 'low' };
  return s;
};

const live = (status: SystemStatus = ready()) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

const running: CalibrationStatus = {
  ...mockCalibration,
  state: 'running',
  active: true,
  ending: null,
  proposal: null,
  legs: [],
  leg: { index: 1, count: 2, name: 'slowOpen', phase: 'drive', elapsedMs: 12000, limitMs: 180000 },
};

const serve = (status: CalibrationStatus) => server.use(http.get('/api/roof/calibration', () => HttpResponse.json(status)));

describe('Calibrate', () => {
  beforeEach(() => {
    live();
    mockDevice();
    serve({ ...mockCalibration, state: 'idle', ending: null, proposal: null, legs: [], request: null });
  });
  afterEach(() => vi.useRealTimers());

  it('starts only once the person confirms they are at the roof, and sends what they said', async () => {
    const bodies: unknown[] = [];
    const headers: (string | null)[] = [];
    server.use(
      http.post('/api/roof/calibration/start', async ({ request }) => {
        bodies.push(await request.json());
        headers.push(request.headers.get('X-RoofRoof'));
        return HttpResponse.json({ success: true, state: 'running' });
      }),
    );
    render(<Calibrate />);
    const start = await screen.findByRole('button', { name: 'Start calibration' });
    expect(start).toBeDisabled();
    expect(screen.getByText('Confirm that you are at the roof first')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('switch', { name: 'I am at the roof and can see it' }));
    await waitFor(() => expect(start).toBeEnabled());
    fireEvent.click(start);
    await waitFor(() => expect(bodies).toHaveLength(1));
    expect(bodies[0]).toEqual({ attended: true, at: 'closed', travelMm: 1850, topPass: false });
    expect(headers[0]).toBe('1');
  });

  it('says why Start is unavailable while a check fails', async () => {
    const s = ready();
    s.roof!.weather = 'unsafe';
    s.roof!.weatherText = 'unsafe: rain';
    live(s);
    render(<Calibrate />);
    fireEvent.click(await screen.findByRole('switch', { name: 'I am at the roof and can see it' }));
    expect(screen.getByRole('button', { name: 'Start calibration' })).toBeDisabled();
    expect(screen.getByText('Not ready: Weather safe')).toBeInTheDocument();
    expect(screen.getByText('unsafe: rain')).toBeInTheDocument();
  });

  it('shows the device refusal', async () => {
    server.use(
      http.post('/api/roof/calibration/start', () =>
        HttpResponse.json({ success: false, reason: 'mountNotParked', error: 'Mount not parked: N.I.N.A. not responding' }, { status: 409 }),
      ),
    );
    render(<Calibrate />);
    fireEvent.click(await screen.findByRole('switch', { name: 'I am at the roof and can see it' }));
    fireEvent.click(screen.getByRole('button', { name: 'Start calibration' }));
    expect(await screen.findByText('Mount not parked: N.I.N.A. not responding')).toBeInTheDocument();
  });

  it('shows the run with Stop enabled, and Stop sends the abort with the header', async () => {
    serve(running);
    const aborts: (string | null)[] = [];
    server.use(
      http.post('/api/roof/calibration/abort', ({ request }) => {
        aborts.push(request.headers.get('X-RoofRoof'));
        return HttpResponse.json({ success: true, state: 'stopped' });
      }),
    );
    render(<Calibrate />);
    const stop = await screen.findByRole('button', { name: 'Stop calibration' });
    expect(stop).toBeEnabled();
    expect(screen.getByText(/Leg 1 of 2: Slow open/)).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Start calibration' })).toBeNull();
    fireEvent.click(stop);
    await waitFor(() => expect(aborts).toEqual(['1']));
  });

  it('shows the proposal beside the saved values and saves it only on Accept', async () => {
    serve(mockCalibration);
    const saves: Record<string, unknown>[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        saves.push((await request.json()) as Record<string, unknown>);
        return HttpResponse.json({ success: true });
      }),
    );
    render(<Calibrate />);
    expect(await screen.findByText('38 → 41')).toBeInTheDocument();
    expect(screen.getByText('15 → 10')).toBeInTheDocument();
    expect(saves).toHaveLength(0);
    fireEvent.click(screen.getByRole('button', { name: 'Accept and save' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    const roof = saves[0].roof as Record<string, unknown>;
    expect(roof.speedMmPerSecond).toBe(41);
    expect(roof.timeoutMarginSeconds).toBe(10);
    expect(await screen.findByText(/Saved/)).toBeInTheDocument();
  });

  it('Discard saves nothing', async () => {
    serve(mockCalibration);
    const saves: unknown[] = [];
    server.use(http.post('/api/config', () => (saves.push(1), HttpResponse.json({ success: true }))));
    render(<Calibrate />);
    fireEvent.click(await screen.findByRole('button', { name: 'Discard' }));
    expect(await screen.findByText(/settings are unchanged/)).toBeInTheDocument();
    expect(saves).toHaveLength(0);
  });

  it('explains a withheld threshold and a failed run proposes nothing', async () => {
    serve({
      ...mockCalibration,
      proposal: {
        ...mockCalibration.proposal!,
        hallHighEnd: null,
        hallHighCounts: null,
        hallLowCounts: null,
        hallProblem: { reason: 'samePole', text: 'Both ends read high: the magnets face the sensor with the same pole' },
      },
    });
    const { unmount } = render(<Calibrate />);
    expect(await screen.findByText(/same pole/)).toBeInTheDocument();
    expect(screen.getAllByText(/not measured/)).toHaveLength(3);
    unmount();
    serve({ ...mockCalibration, state: 'failed', proposal: null, ending: { reason: 'fault', text: 'Fault on the slow open: timeout' } });
    render(<Calibrate />);
    expect(await screen.findByText('Fault on the slow open: timeout')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Accept and save' })).toBeNull();
  });
});
