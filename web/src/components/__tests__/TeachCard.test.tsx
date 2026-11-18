import { beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Calibrate from '../Calibrate';
import type { SystemStatus, TeachStatus } from '../../types';
import { mockCalibration, mockStatus, mockTeach } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 019: the Teach card. The hold buttons need every check and the
// confirmation; a press starts a jog, keep-alives renew it with its id while
// held, a release sends Stop; marks, the proposal and its confirm.

const ready = (state: 'closed' | 'opening' = 'closed'): SystemStatus => {
  const s = structuredClone(mockStatus);
  s.roof = { ...s.roof!, weather: 'safe', parkConfirmed: true, fault: null, closePhase: 'idle', supplyLow: false, state };
  s.roof.hall = { counts: 700, level: 'low' };
  return s;
};

const live = (status: SystemStatus = ready()) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

const teachCard = () => within(document.getElementById('teach')!);
const serveTeach = (status: TeachStatus) => server.use(http.get('/api/roof/teach', () => HttpResponse.json(status)));

interface Recorded {
  path: string;
  body: unknown;
  header: string | null;
}

const record = () => {
  const posts: Recorded[] = [];
  const keep = async (request: Request) => {
    const text = await request.text();
    posts.push({ path: new URL(request.url).pathname, body: text ? JSON.parse(text) : null, header: request.headers.get('X-RoofRoof') });
  };
  server.use(
    http.post('/api/roof/jog', async ({ request }) => {
      await keep(request);
      return HttpResponse.json({ success: true, state: 'opening', id: 4, holdMs: 500, keepAliveMs: 150, limitMs: 30000 });
    }),
    http.post('/api/roof/stop', async ({ request }) => {
      await keep(request);
      return HttpResponse.json({ success: true, state: 'unknown' });
    }),
    http.post('/api/roof/teach/:action', async ({ request }) => {
      await keep(request);
      return HttpResponse.json({ success: true, state: 'closed' });
    }),
  );
  return posts;
};

const attend = async () => fireEvent.click(await teachCard().findByRole('switch', { name: 'I am at the roof, watching it' }));

describe('TeachCard', () => {
  beforeEach(() => {
    live();
    mockDevice();
    server.use(
      http.get('/api/roof/calibration', () =>
        HttpResponse.json({ ...mockCalibration, state: 'idle', ending: null, proposal: null, legs: [], request: null }),
      ),
    );
    serveTeach(mockTeach);
  });

  it('enables the hold buttons only once the person confirms they are at the roof', async () => {
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    const open = await teachCard().findByRole('button', { name: 'Hold to open' });
    expect(open).toBeDisabled();
    expect(teachCard().getByText('Confirm that you are at the roof first')).toBeInTheDocument();
    await attend();
    await waitFor(() => expect(open).toBeEnabled());
    expect(teachCard().getByRole('button', { name: 'Hold to close' })).toBeEnabled();
  });

  it('says why the hold buttons are unavailable while a check fails', async () => {
    const s = ready();
    s.roof!.parkConfirmed = false;
    s.roof!.parkText = 'not parked: N.I.N.A. not responding';
    live(s);
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    await attend();
    expect(teachCard().getByRole('button', { name: 'Hold to open' })).toBeDisabled();
    expect(teachCard().getByText('Not ready: Mount parked')).toBeInTheDocument();
  });

  it('a press starts a jog, keep-alives renew it with its id, and the release stops that jog', async () => {
    const posts = record();
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    await attend();
    const open = teachCard().getByRole('button', { name: 'Hold to open' });
    fireEvent.pointerDown(open);
    await waitFor(() => expect(posts.filter((p) => p.path === '/api/roof/jog').length).toBeGreaterThanOrEqual(3), { timeout: 2000 });
    expect(posts[0]).toEqual({ path: '/api/roof/jog', body: { direction: 'open', attended: true }, header: '1' });
    expect(posts[1]).toEqual({ path: '/api/roof/jog', body: { direction: 'open', attended: true, id: 4 }, header: '1' });
    expect(open).toHaveAttribute('aria-pressed', 'true');
    fireEvent.pointerUp(open);
    await waitFor(() => expect(posts.at(-1)?.body).toEqual({ direction: 'open', attended: true, id: 4, release: true }));
    const count = posts.length;
    await new Promise((resolve) => setTimeout(resolve, 400));
    expect(posts.length).toBe(count); // no keep-alive after the release
    expect(open).toHaveAttribute('aria-pressed', 'false');
  });

  it('leaving the button or hiding the page releases it', async () => {
    const posts = record();
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    await attend();
    const released = () => posts.filter((p) => (p.body as { release?: boolean } | null)?.release === true);
    const close = teachCard().getByRole('button', { name: 'Hold to close' });
    fireEvent.pointerDown(close);
    await waitFor(() => expect(posts.length).toBeGreaterThanOrEqual(2));
    fireEvent.pointerLeave(close);
    await waitFor(() => expect(released()).toHaveLength(1));
    fireEvent.pointerDown(close);
    await waitFor(() => expect(posts.filter((p) => p.path === '/api/roof/jog').length).toBeGreaterThanOrEqual(5));
    Object.defineProperty(document, 'visibilityState', { value: 'hidden', configurable: true });
    document.dispatchEvent(new Event('visibilitychange'));
    await waitFor(() => expect(released()).toHaveLength(2));
    Object.defineProperty(document, 'visibilityState', { value: 'visible', configurable: true });
  });

  it('shows the device refusal of a jog and sends no keep-alive', async () => {
    const jogs: unknown[] = [];
    server.use(
      http.post('/api/roof/jog', async ({ request }) => {
        jogs.push(await request.json());
        return HttpResponse.json(
          { success: false, reason: 'mountNotParked', error: 'Mount not parked: N.I.N.A. not responding' },
          { status: 409 },
        );
      }),
    );
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    await attend();
    const open = teachCard().getByRole('button', { name: 'Hold to open' });
    fireEvent.pointerDown(open);
    expect(await teachCard().findByText('Mount not parked: N.I.N.A. not responding')).toBeInTheDocument();
    await new Promise((resolve) => setTimeout(resolve, 400));
    expect(jogs).toHaveLength(1);
    expect(open).toHaveAttribute('aria-pressed', 'false');
  });

  it('shows Stop, enabled, while the roof moves', async () => {
    live(ready('opening'));
    const posts = record();
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    const stop = await teachCard().findByRole('button', { name: 'Stop' });
    expect(stop).toBeEnabled();
    fireEvent.click(stop);
    await waitFor(() => expect(posts.at(-1)).toEqual({ path: '/api/roof/stop', body: null, header: '1' }));
  });

  it('marks a place and shows the readings taken', async () => {
    const posts = record();
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    expect(await teachCard().findByText('712 counts (noise 1.6)')).toBeInTheDocument();
    expect(teachCard().getByText('Not marked yet')).toBeInTheDocument();
    fireEvent.click(teachCard().getByRole('button', { name: 'This is open' }));
    await waitFor(() => expect(posts).toEqual([{ path: '/api/roof/teach/mark', body: { at: 'open' }, header: '1' }]));
  });

  it('marks wait until the roof has been still', async () => {
    serveTeach({ ...mockTeach, rest: { ready: false, counts: null } });
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    await waitFor(() => expect(teachCard().getByRole('button', { name: 'This is closed' })).toBeDisabled());
    expect(teachCard().getByText('Marks are taken with the roof still for a second.')).toBeInTheDocument();
  });

  it('confirms the proposal shown, with its values', async () => {
    serveTeach({ ...mockTeach, proposal: { ok: true, hallHighEnd: 'closed', hallHighCounts: 2725, hallLowCounts: 1375 } });
    const posts = record();
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    const confirm = await teachCard().findByRole('button', { name: 'Confirm and save' });
    expect(teachCard().getByText('3000 → 2725')).toBeInTheDocument();
    fireEvent.click(confirm);
    await waitFor(() =>
      expect(posts).toEqual([
        { path: '/api/roof/teach/confirm', body: { hallHighEnd: 'closed', hallHighCounts: 2725, hallLowCounts: 1375 }, header: '1' },
      ]),
    );
    expect(await teachCard().findByText('Saved. The roof now finds its ends with these settings.')).toBeInTheDocument();
  });

  it('a refused proposal says why and offers to start again', async () => {
    serveTeach({ ...mockTeach, proposal: { ok: false, reason: 'samePole', text: 'Both ends read high' } });
    const posts = record();
    render(<Calibrate />);
    await screen.findByRole('heading', { level: 2, name: 'Teach the ends' });
    expect(await teachCard().findByText('Both ends read high')).toBeInTheDocument();
    expect(teachCard().queryByRole('button', { name: 'Confirm and save' })).toBeNull();
    fireEvent.click(teachCard().getByRole('button', { name: 'Start again' }));
    await waitFor(() => expect(posts).toEqual([{ path: '/api/roof/teach/discard', body: null, header: '1' }]));
  });
});
