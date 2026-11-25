import { describe, expect, it, vi } from 'vitest';
import { render, screen } from '@testing-library/preact';
import Dashboard from '../Dashboard';

// Spec 028 FR-005: no reading from the current connection, no state; Stop stays.
vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

describe('Dashboard without a live reading', () => {
  it('says Connecting before the first reading, with no roof state and Stop available', () => {
    vi.mocked(useWebSocket).mockReturnValue({ data: null, connected: false, lastMessageAt: null, reconnecting: false });
    render(<Dashboard />);
    expect(screen.getByRole('heading', { name: 'Connecting' })).toBeInTheDocument();
    expect(screen.queryByText('Closed')).toBeNull();
    expect(screen.getByRole('button', { name: 'Open' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Stop' })).toBeEnabled();
  });

  it('says Reconnecting after a drop, never the old readings', () => {
    vi.mocked(useWebSocket).mockReturnValue({ data: null, connected: false, lastMessageAt: null, reconnecting: true });
    render(<Dashboard />);
    expect(screen.getByRole('heading', { name: 'Reconnecting' })).toBeInTheDocument();
    expect(screen.queryByText('Device & Network')).toBeNull();
  });
});
