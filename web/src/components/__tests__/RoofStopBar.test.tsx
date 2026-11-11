import { beforeEach, describe, expect, it, vi } from 'vitest';
import { act, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import { server } from '../../test/mswServer';
import { mockStatus } from '../../mocks/data';
import type { RoofStatus, SystemStatus } from '../../types';

// Stop on every page while the roof moves (constitution VII; spec 009 FR-014).

let current: SystemStatus | null = null;
let connected = true;
vi.mock('../../hooks/useWebSocket', () => ({
  useWebSocket: () => ({ data: current, connected, lastMessageAt: 0 }),
}));

const { default: RoofStopBar } = await import('../RoofStopBar');
const { default: App } = await import('../../App');

const withRoof = (roof: Partial<RoofStatus>) => {
  const s = structuredClone(mockStatus);
  s.roof = { ...s.roof!, ...roof };
  return s;
};

describe('RoofStopBar', () => {
  beforeEach(() => {
    current = null;
    connected = true;
  });

  it.each([
    ['opening', { state: 'opening' }],
    ['closing', { state: 'closing' }],
    ['a close waiting', { state: 'open', closePending: true }],
    ['a calibration run pauses at an end', { state: 'open', calibrating: true }],
  ] as [string, Partial<RoofStatus>][])('shows Stop while %s', (_, roof) => {
    current = withRoof(roof);
    render(<RoofStopBar />);
    expect(screen.getByRole('region', { name: 'Roof moving' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Stop roof' })).toBeEnabled();
    expect(document.body.classList.contains('has-stop-bar')).toBe(true);
  });

  it('stays while the live data is lost, if the roof was last seen moving', () => {
    current = withRoof({ state: 'closing' });
    connected = false;
    render(<RoofStopBar />);
    expect(screen.getByRole('button', { name: 'Stop roof' })).toBeEnabled();
  });

  it.each(['closed', 'open', 'stopped', 'unknown'] as const)('is not there while the roof is %s', (state) => {
    current = withRoof({ state });
    render(<RoofStopBar />);
    expect(screen.queryByRole('button', { name: 'Stop roof' })).toBeNull();
    expect(document.body.classList.contains('has-stop-bar')).toBe(false);
  });

  it('shows Stop when no status has come for 5 s: the roof\u2019s state is unknown', () => {
    vi.useFakeTimers();
    render(<RoofStopBar />);
    expect(screen.queryByRole('button', { name: 'Stop roof' })).toBeNull();
    act(() => {
      vi.advanceTimersByTime(5000);
    });
    expect(screen.getByRole('button', { name: 'Stop roof' })).toBeEnabled();
    expect(screen.getByText('No live data')).toBeInTheDocument();
    vi.useRealTimers();
  });

  it('sends stop with the command header', async () => {
    let header: string | null = null;
    server.use(
      http.post('/api/roof/stop', ({ request }) => {
        header = request.headers.get('X-RoofRoof');
        return HttpResponse.json({ success: true, state: 'stopped' });
      }),
    );
    current = withRoof({ state: 'opening' });
    render(<RoofStopBar />);
    fireEvent.click(screen.getByRole('button', { name: 'Stop roof' }));
    await waitFor(() => expect(header).toBe('1'));
  });

  // Every route renders inside Layout, which holds the bar (SC-003).
  it.each(['/', '/alpaca', '/settings', '/system', '/updates', '/wifi', '/no-such-page'])('is on %s', async (path) => {
    vi.stubGlobal(
      'ResizeObserver',
      class {
        observe() {}
        unobserve() {}
        disconnect() {}
      },
    );
    current = withRoof({ state: 'closing' });
    window.history.replaceState(null, '', path);
    render(<App />);
    expect(await screen.findByRole('button', { name: 'Stop roof' })).toBeEnabled();
    window.history.replaceState(null, '', '/');
    vi.unstubAllGlobals();
  });
});
