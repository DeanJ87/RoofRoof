import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, within } from '@testing-library/preact';
import Diagnostics from '../Diagnostics';
import Settings from '../Settings';
import { mockDiag } from '../../mocks/handlers';
import { mockDiagLive } from '../../mocks/diag';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig } from '../../mocks/data';

// The Diagnostics page and its settings (spec 017 FR-030, FR-032, FR-033).

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn(() => ({ data: null, connected: false, lastMessageAt: null })) }));
import { useWebSocket } from '../../hooks/useWebSocket';

afterEach(() => {
  mockDiag.enabled = false;
  vi.mocked(useWebSocket).mockClear();
});

describe('Diagnostics page', () => {
  it('says the mode is off, links its setting and opens no stream', async () => {
    render(<Diagnostics />);
    expect(await screen.findByText(/Advanced diagnostics is off/)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Turn on' })).toBeInTheDocument();
    expect(useWebSocket).not.toHaveBeenCalled();
  });

  describe('with the mode on', () => {
    beforeEach(() => {
      mockDiag.enabled = true;
    });

    it('streams the live tiles and lists the moves, the kept one without a profile', async () => {
      render(<Diagnostics />);
      expect(await screen.findByRole('heading', { name: 'Control task' })).toBeInTheDocument();
      expect(useWebSocket).toHaveBeenCalledWith('/ws/diag');
      const table = await screen.findByRole('table');
      const rows = within(table).getAllByRole('row');
      expect(rows).toHaveLength(3); // header, move 11, kept move 4
      expect(within(rows[1]).getByRole('button', { name: 'Show move 11' })).toBeInTheDocument();
      expect(within(rows[1]).getByText('Arrived')).toBeInTheDocument();
      expect(within(rows[2]).queryByRole('button')).toBeNull();
      expect(within(rows[2]).getByText('Kept')).toBeInTheDocument();
      expect(within(rows[2]).getByText('Fault')).toBeInTheDocument();
    });

    it("shows a chosen move's chart with its downloads", async () => {
      render(<Diagnostics />);
      fireEvent.click(await screen.findByRole('button', { name: 'Show move 11' }));
      expect(await screen.findByRole('heading', { name: 'Move 11' })).toBeInTheDocument();
      expect(await screen.findByRole('img', { name: /current averages/ })).toBeInTheDocument();
      expect(screen.getByRole('link', { name: 'Download CSV' })).toHaveAttribute('href', '/api/diag/moves/11?format=csv');
      expect(screen.getByRole('link', { name: 'Download JSON' })).toHaveAttribute('href', '/api/diag/moves/11');
    });

    it('keeps Clear log unavailable while the roof moves, with the reason', async () => {
      render(<Diagnostics />);
      const clear = await screen.findByRole('button', { name: 'Clear log' });
      expect(mockDiagLive.roof.state).toBe('opening');
      expect(clear).toBeDisabled();
      expect(clear).toHaveAccessibleDescription("The log can't be cleared while the roof moves.");
    });

    it('says when current sense is not fitted', async () => {
      vi.mocked(useWebSocket).mockReturnValue({
        data: { ...mockDiagLive, roof: { ...mockDiagLive.roof, currentSense: false, motorAmps: null, powerWatts: null } },
        connected: true,
        lastMessageAt: Date.now(),
      });
      render(<Diagnostics />);
      expect(await screen.findAllByText('Not fitted')).toHaveLength(2);
    });
  });
});

describe('Settings → Device → Diagnostics', () => {
  const open = () => {
    window.history.replaceState(null, '', '/settings?tab=device');
    render(<Settings />);
  };

  it("can't switch on the two options while the mode is off, and says why (D-46)", async () => {
    mockDevice();
    open();
    const keep = await screen.findByRole('switch', { name: 'Keep move summaries' });
    const mqtt = screen.getByRole('switch', { name: 'Diagnostics over MQTT' });
    expect(keep).toBeDisabled();
    expect(mqtt).toBeDisabled();
    expect(screen.getAllByText('Advanced diagnostics is off.')).toHaveLength(2);
    fireEvent.click(screen.getByRole('switch', { name: 'Advanced diagnostics' }));
    expect(keep).toBeEnabled();
  });

  it('shows MQTT as the reason once the mode is on and MQTT is off', async () => {
    mockDevice({ config: { diag: { enabled: true, keepSummaries: true, mqtt: true }, mqtt: { ...mockConfig.mqtt, enabled: false } } });
    open();
    await screen.findByRole('switch', { name: 'Diagnostics over MQTT' });
    expect(screen.getByText(/Inactive: MQTT is off/)).toBeInTheDocument();
    expect(screen.queryByText('Advanced diagnostics is off.')).toBeNull();
  });
});
