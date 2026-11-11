import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import SetupPrompt, { resetSetupPrompt } from '../SetupPrompt';
import { mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

const routeMock = vi.hoisted(() => vi.fn());
vi.mock('preact-router', async (original) => ({
  ...(await original<typeof import('preact-router')>()),
  route: routeMock,
  useRouter: () => [{ url: window.location.pathname }],
}));

// Spec 015 FR-003, FR-004: a new device opens the wizard once per page load
// (never from the hotspot); afterwards, and after an upgrade, a banner offers it.

const settle = () => new Promise((resolve) => setTimeout(resolve, 50));

describe('SetupPrompt', () => {
  beforeEach(() => {
    resetSetupPrompt();
    routeMock.mockClear();
  });
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('opens the wizard on the first dashboard visit of a new device, then shows the banner', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 2 } } });
    const first = render(<SetupPrompt />);
    await waitFor(() => expect(routeMock).toHaveBeenCalledWith('/wizard'));
    first.unmount();
    routeMock.mockClear();
    render(<SetupPrompt />);
    expect(await screen.findByText(/Setup isn't finished/)).toBeInTheDocument();
    expect(routeMock).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Continue setup' }));
    expect(routeMock).toHaveBeenCalledWith('/wizard');
  });

  it('never opens it from the setup hotspot', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, wifi: { ...mockStatus.wifi, apMode: true } })));
    render(<SetupPrompt />);
    expect(await screen.findByText(/Setup isn't finished/)).toBeInTheDocument();
    expect(routeMock).not.toHaveBeenCalled();
  });

  it('offers it after an upgrade without opening it, and Dismiss sets it done', async () => {
    mockDevice({ config: { setup: { state: 'offered', step: 0 } } });
    const saves: unknown[] = [];
    server.use(
      http.post('/api/config', async ({ request }) => {
        saves.push(await request.json());
        return HttpResponse.json({ success: true });
      }),
    );
    const { container } = render(<SetupPrompt />);
    expect(await screen.findByText(/A setup wizard can check/)).toBeInTheDocument();
    expect(routeMock).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Dismiss' }));
    await waitFor(() => expect(container.querySelector('[data-inventory="setup-banner"]')).toBeNull());
    expect(saves).toEqual([{ setup: { state: 'done', step: 0 } }]);
  });

  it.each([
    ['done', { setup: { state: 'done', step: 0 } }],
    ['firmware without a setup state', {}],
  ])('shows nothing when %s', async (_name, config) => {
    mockDevice({ config });
    const { container } = render(<SetupPrompt />);
    await settle();
    expect(container.querySelector('[data-inventory="setup-banner"]')).toBeNull();
    expect(routeMock).not.toHaveBeenCalled();
  });

  it('shows nothing away from the dashboard', async () => {
    mockDevice({ config: { setup: { state: 'offered', step: 0 } } });
    window.history.replaceState(null, '', '/settings');
    const { container } = render(<SetupPrompt />);
    await settle();
    expect(container.querySelector('[data-inventory="setup-banner"]')).toBeNull();
  });
});
