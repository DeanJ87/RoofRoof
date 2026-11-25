import { describe, it, expect, vi } from 'vitest';
import { fireEvent, render, screen, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';
import { ethernetFor } from '../../mocks/pins';
import { Toaster } from '../toast';
import System from '../System';
import type { SystemStatus } from '../../types';
import { mockStatus } from '../../mocks/data';

vi.mock('../../hooks/useWebSocket', () => ({
  useWebSocket: vi.fn(),
}));

import { useWebSocket } from '../../hooks/useWebSocket';

describe('System', () => {
  it('shows a loading state while disconnected', () => {
    vi.mocked(useWebSocket).mockReturnValue({ data: null, connected: false, lastMessageAt: null });

    render(<System />);

    expect(screen.getByText('Connecting')).toBeInTheDocument();
  });

  // Spec 028: after a drop no values from the old connection, and it says so.
  it('says Reconnecting after the stream drops', () => {
    vi.mocked(useWebSocket).mockReturnValue({ data: null, connected: false, lastMessageAt: null, reconnecting: true });
    render(<System />);
    expect(screen.getByRole('heading', { name: 'Reconnecting' })).toBeInTheDocument();
  });

  it('renders firmware version once connected', () => {
    vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({
      data: mockStatus,
      connected: true,
      lastMessageAt: Date.now(),
    });

    render(<System />);

    expect(screen.getByText(`v${mockStatus.firmware!.version}`)).toBeInTheDocument();
  });

  it('lists the IPv6 addresses with their scope (SQMeter spec 015)', () => {
    vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({
      data: {
        ...mockStatus,
        wifi: {
          ...mockStatus.wifi,
          ipv6: {
            enabled: true,
            addresses: [
              { address: 'fe80::a00:27ff:fe4e:66a1', scope: 'link-local' },
              { address: '2a02:8010:abcd:1::9', scope: 'global' },
            ],
          },
        },
      },
      connected: true,
      lastMessageAt: Date.now(),
    });

    render(<System />);

    expect(screen.getByText('IPv6 (link-local)')).toBeInTheDocument();
    expect(screen.getByText('fe80::a00:27ff:fe4e:66a1')).toBeInTheDocument();
    expect(screen.getByText('IPv6 (global)')).toBeInTheDocument();
    expect(screen.getByText('2a02:8010:abcd:1::9')).toBeInTheDocument();
  });

  it('runs the setup wizard again from the Setup card (spec 015 FR-006)', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } });
    vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: mockStatus, connected: true, lastMessageAt: Date.now() });
    render(<System />);
    expect(screen.getByRole('button', { name: 'Run setup wizard' })).toBeInTheDocument();
    expect(await screen.findByText('Done')).toBeInTheDocument();
  });

  const show = (status: SystemStatus) => {
    vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });
    return render(<System />);
  };
  const cardOf = (title: string) => within(screen.getByRole('heading', { name: title }).closest('section') as HTMLElement);

  it('on Ethernet, WiFi not connected is neutral, not red (spec 023)', () => {
    const wifi = { ...mockStatus.wifi, connected: false, ssid: '', ip: '0.0.0.0' };
    const { unmount } = show({ ...mockStatus, wifi, ethernet: ethernetFor('wt32-eth01') });
    expect(cardOf('WiFi').getByText('Not connected')).not.toHaveClass('tone-red');
    unmount();
    show({ ...mockStatus, wifi, ethernet: ethernetFor('wt32-eth01', 'ethernet') });
    expect(cardOf('WiFi').getByText('Off (Ethernet)')).not.toHaveClass('tone-red');
  });

  it('a DevKit with WiFi down still says Disconnected in red', () => {
    show({ ...mockStatus, wifi: { ...mockStatus.wifi, connected: false } });
    expect(cardOf('WiFi').getByText('Disconnected')).toHaveClass('tone-red');
  });

  it('in WiFi-only mode the Ethernet card says Off, not No link', () => {
    show({
      ...mockStatus,
      ethernet: ethernetFor('wt32-eth01', 'wifi'),
      pins: { preset: 'wt32-carrier-v1', board: 'wt32-eth01', valid: true, restartNeeded: false },
    });
    const card = cardOf('Ethernet');
    expect(card.getByText('Off (WiFi only)')).toBeInTheDocument();
    expect(card.queryByText('No link')).toBeNull();
  });

  it('a refused restart shows the device reason', async () => {
    server.use(http.post('/api/restart', () => HttpResponse.json({ error: 'The roof is moving: wait until it stops' }, { status: 409 })));
    render(<Toaster />);
    show(mockStatus);
    fireEvent.click(screen.getByRole('button', { name: 'Restart device' }));
    fireEvent.click(screen.getByRole('button', { name: 'Restart now' }));
    expect(await screen.findByText('The roof is moving: wait until it stops')).toBeInTheDocument();
    expect(screen.queryByText('Restarting...')).toBeNull();
  });
});
