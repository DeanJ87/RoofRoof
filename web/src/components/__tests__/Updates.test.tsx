import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/preact';
import Updates from '../Updates';

vi.mock('../../hooks/useWebSocket', () => ({
  useWebSocket: vi.fn(),
}));

import { useWebSocket } from '../../hooks/useWebSocket';

// RoofRoof updates by upload only: no release check (spec 001).
describe('Updates - manual upload', () => {
  beforeEach(() => {
    vi.mocked(useWebSocket).mockReturnValue({
      data: { firmware: { name: 'RoofRoof', product: 'roofroof', version: '0.1.0', buildDate: 'x', buildTime: 'y' } },
      connected: true,
      lastMessageAt: Date.now(),
    });
  });

  it('shows the installed version', () => {
    render(<Updates />);
    expect(screen.getByText('v0.1.0')).toBeInTheDocument();
  });

  it('has no release check', () => {
    render(<Updates />);
    expect(screen.queryByLabelText('Release track')).toBeNull();
  });

  it('names the file for the chosen image', () => {
    render(<Updates />);
    expect(screen.getByRole('button', { name: /firmware\.bin/ })).toBeDisabled(); // no file chosen yet
    fireEvent.change(screen.getByLabelText('Image'), { target: { value: 'filesystem' } });
    expect(screen.getByRole('button', { name: /littlefs\.bin/ })).toBeInTheDocument();
  });
});
