import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/preact';
import DeviceCard from '../DeviceCard';
import { mockStatus } from '../../mocks/data';
import { ethernetFor } from '../../mocks/pins';

// Spec 023 FR-028: on a board with Ethernet the Device & Network card shows its
// link and address; the WiFi rows stay. The demo's board is a DevKit.

describe('DeviceCard', () => {
  it('shows Ethernet only on a board that has it', () => {
    // inventory: ethernet
    const { unmount } = render(<DeviceCard status={mockStatus} />);
    expect(screen.queryByText('Ethernet')).toBeNull();
    unmount();
    render(<DeviceCard status={{ ...mockStatus, ethernet: ethernetFor('wt32-eth01') }} />);
    expect(screen.getByText('Ethernet').closest('.reading-row')).toHaveTextContent('Connected');
    expect(screen.getByText('192.168.1.60')).toBeInTheDocument();
    expect(screen.getByText('WiFi IP')).toBeInTheDocument(); // the WiFi rows stay, named as WiFi's
  });

  it("on Ethernet, WiFi that isn't connected is neutral: no signal and no 0.0.0.0 address", () => {
    const wifi = { ...mockStatus.wifi, connected: false, ssid: '', ip: '0.0.0.0', rssi: 0 };
    const { unmount } = render(<DeviceCard status={{ ...mockStatus, wifi, ethernet: ethernetFor('wt32-eth01') }} />);
    const pill = screen.getByText('Not connected');
    expect(pill).toHaveClass('pill-dim');
    expect(screen.queryByText('Strong')).toBeNull();
    expect(screen.queryByText(/dBm/)).toBeNull();
    expect(screen.queryByText('0.0.0.0')).toBeNull();
    expect(screen.queryByText('WiFi IP')).toBeNull();
    unmount();
    // Ethernet only: WiFi is off.
    render(<DeviceCard status={{ ...mockStatus, wifi, ethernet: ethernetFor('wt32-eth01', 'ethernet') }} />);
    expect(screen.getByText('Off (Ethernet)')).toHaveClass('pill-dim');
  });

  it('says when the Ethernet cable has no link', () => {
    render(<DeviceCard status={{ ...mockStatus, ethernet: { ...ethernetFor('wt32-eth01'), link: false, ip: '' } }} />);
    expect(screen.getByText('Ethernet').closest('.reading-row')).toHaveTextContent('No link');
  });
});
