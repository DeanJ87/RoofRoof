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
    expect(screen.getByText('IPv4')).toBeInTheDocument(); // the WiFi rows stay
  });

  it('says when the Ethernet cable has no link', () => {
    render(<DeviceCard status={{ ...mockStatus, ethernet: { ...ethernetFor('wt32-eth01'), link: false, ip: '' } }} />);
    expect(screen.getByText('Ethernet').closest('.reading-row')).toHaveTextContent('No link');
  });
});
