import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Wizard from '../Wizard';
import { SCREENS } from '../wizard/screens';
import type { PinsConfig, SystemStatus } from '../../types';
import { mockStatus } from '../../mocks/data';
import { BOARD_DETECTED_TEXT, ethernetFor, mockValidate, mockWiring, pinsDocumentFor } from '../../mocks/pins';
import { mockBoard } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 023 FR-027: the Hardware page's board question first, the network mode
// on a board with Ethernet, and the wiring check; the rain contact question is gone.

const live = (status: SystemStatus) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

const recordSaves = () => {
  const bodies: Record<string, unknown>[] = [];
  server.use(
    http.post('/api/config', async ({ request }) => {
      bodies.push((await request.json()) as Record<string, unknown>);
      return HttpResponse.json({ success: true });
    }),
  );
  return bodies;
};

const at = (query: string) => window.history.replaceState(null, '', `/wizard?${query}`);
const onScreen = (id: string) =>
  waitFor(() => expect(document.querySelector('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id));
const button = (name: string) => screen.getByRole('button', { name });
const NEW = { setup: { state: 'new' as const, step: 2 } };

describe('Wizard: board, network and wiring', () => {
  beforeEach(() => {
    mockWiring.state = { state: 'idle', checks: [] };
  });
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('has no rain contact screen, and the board first on the Hardware page', () => {
    const ids = SCREENS.map((screen) => screen.id);
    expect(ids).not.toContain('rain');
    expect(SCREENS.filter((screen) => screen.page === 'hardware').map((screen) => screen.id)).toEqual([
      'board',
      'network',
      'supply',
      'current',
      'parkSwitches',
      'wiring',
    ]);
  });

  it('a new device whose probe found a WT32-ETH01 has its preset chosen, saved on Continue', async () => {
    const { status } = mockBoard('unset', NEW);
    live(status);
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('board');
    expect(screen.getByRole('dialog', { name: 'Which board is RoofRoof on?' })).toBeInTheDocument();
    expect(screen.getByText('No board is chosen yet, so nothing is driven: choose one, then restart.')).toBeInTheDocument();
    await waitFor(() => expect(screen.getByRole('radio', { name: /^WT32-ETH01/ })).toBeChecked());
    expect(screen.getByText('Saved when you continue; the board applies at a restart.')).toBeInTheDocument();
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].pins).toMatchObject({ preset: 'wt32-carrier-v1', board: 'wt32-eth01', enable: 4 });
    // The detected WT32-ETH01 is the board in effect: its network question follows.
    await onScreen('network');
  });

  it("once a WT32-ETH01 is detected, the DevKit can't be chosen and says why", async () => {
    const { status } = mockBoard('unset', NEW);
    live(status);
    render(<Wizard />);
    await onScreen('board');
    const devkit = screen.getByRole('radio', { name: /^ESP32 DevKit/ });
    expect(devkit).toBeDisabled();
    expect(devkit).toHaveAccessibleDescription(/Not this board: its Ethernet answered, so it is a WT32-ETH01\./);
  });

  it('a DevKit map saved on a detected WT32-ETH01 shows the device error', async () => {
    const { status } = mockBoard('devkit', NEW);
    // The board's Ethernet answered: the device has evidence it is a WT32-ETH01.
    live({ ...status, ethernet: ethernetFor('wt32-eth01') });
    server.use(
      http.post('/api/pins/validate', async ({ request }) =>
        HttpResponse.json(mockValidate((await request.json()) as Partial<PinsConfig>, 'wt32-eth01')),
      ),
    );
    render(<Wizard />);
    await onScreen('board');
    expect(await screen.findByText(BOARD_DETECTED_TEXT)).toBeInTheDocument();
    expect(screen.getByText(BOARD_DETECTED_TEXT).closest('[role="status"]')).not.toBeNull();
  });

  it('with nothing found, the DevKit is chosen', async () => {
    const { status } = mockBoard('unset', NEW);
    server.use(
      http.get('/api/pins', () => HttpResponse.json({ ...pinsDocumentFor('unset'), board: 'esp32-devkit', detected: null })),
      http.post('/api/pins/validate', async ({ request }) =>
        HttpResponse.json(mockValidate((await request.json()) as Partial<PinsConfig>)),
      ),
    );
    live({ ...status, ethernet: ethernetFor('esp32-devkit'), pins: { ...status.pins!, board: 'esp32-devkit' } });
    render(<Wizard />);
    await onScreen('board');
    expect(await screen.findByText('No Ethernet chip was found, so the ESP32 DevKit is chosen.')).toBeInTheDocument();
    await waitFor(() => expect(screen.getByRole('radio', { name: /^ESP32 DevKit/ })).toBeChecked());
    expect(button('Continue')).toBeEnabled();
  });

  it('without the pin document, no board is chosen until the person answers', async () => {
    const { status } = mockBoard('unset', NEW);
    server.use(http.get('/api/pins', () => HttpResponse.json({ error: 'busy' }, { status: 500 })));
    live(status);
    render(<Wizard />);
    await onScreen('board');
    expect(button('Continue')).toBeDisabled();
    fireEvent.click(screen.getByRole('radio', { name: /^WT32-ETH01/ }));
    expect(button('Continue')).toBeEnabled();
  });

  it('Custom points to the pins page', async () => {
    const { status } = mockBoard('devkit', NEW);
    live(status);
    render(<Wizard />);
    await onScreen('board');
    fireEvent.click(screen.getByRole('radio', { name: /^Custom/ }));
    expect(screen.getByText('Set each pin on the pins page.')).toBeInTheDocument();
    expect(button('Open the pins page')).toBeInTheDocument();
  });

  it('with the saved board not in effect, offers the restart', async () => {
    const { status } = mockBoard('devkit', NEW);
    live({ ...status, pins: { ...status.pins!, preset: 'unset' } });
    render(<Wizard />);
    await onScreen('board');
    const note = screen.getByText('The board you chose applies at a restart.');
    expect(within(note).getByRole('button', { name: 'Restart' })).toBeInTheDocument();
  });

  it('a DevKit skips the network question', async () => {
    const { status } = mockBoard('devkit', NEW);
    live(status);
    render(<Wizard />);
    await onScreen('board');
    fireEvent.click(button('Skip for now'));
    await onScreen('supply');
  });

  it('on Ethernet: says so, and saves the network mode chosen', async () => {
    const { status } = mockBoard('wt32', NEW);
    live(status);
    const saves = recordSaves();
    at('screen=network');
    render(<Wizard />);
    await onScreen('network');
    expect(screen.getByText("You're on Ethernet at 192.168.1.60.")).toBeInTheDocument();
    expect(screen.getByText('WiFi is optional: skip it if Ethernet is all you need.')).toBeInTheDocument();
    expect(screen.getByRole('radio', { name: /^Auto/ })).toBeChecked();
    fireEvent.click(screen.getByRole('radio', { name: /^Ethernet only/ }));
    expect(screen.getByText('Saved when you continue; the mode applies at a restart.')).toBeInTheDocument();
    fireEvent.click(button('Continue'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].network).toEqual({ mode: 'ethernet' });
  });

  it('the wiring check runs on its screen, which can be skipped', async () => {
    const { status } = mockBoard('devkit', NEW);
    live(status);
    at('screen=wiring');
    render(<Wizard />);
    await onScreen('wiring');
    expect(button('Skip for now')).toBeInTheDocument();
    fireEvent.click(await screen.findByRole('button', { name: 'Check wiring' }));
    await waitFor(() => expect(document.querySelector('[data-wiring-check="enPulldown"]')).not.toBeNull());
    expect(screen.getByText('EN reads low with the internal pull-up: its pull-down holds the driver off')).toBeInTheDocument();
  });

  it('the wiring check waits for the restart after a board change', async () => {
    mockBoard('devkit', NEW);
    server.use(http.get('/api/pins', () => HttpResponse.json({ ...pinsDocumentFor('devkit'), restartNeeded: true })));
    live({ ...mockStatus, pins: { ...mockStatus.pins!, restartNeeded: true } });
    at('screen=wiring');
    render(<Wizard />);
    await onScreen('wiring');
    expect(await screen.findByText("Restart first: the saved pins aren't in effect yet.")).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Check wiring' })).toBeDisabled();
    expect(screen.getByText('The board you chose applies at a restart.')).toBeInTheDocument();
  });
});
