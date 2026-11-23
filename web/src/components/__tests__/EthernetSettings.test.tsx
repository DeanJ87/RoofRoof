import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import WifiSetup from '../WifiSetup';
import { mockConfig } from '../../mocks/data';
import { mockBoard } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

// Spec 023 FR-028: Settings → Network and the WiFi setup page on a board with
// Ethernet; nothing new on a board without it. Settings → Roof links the Pins page.

const openTab = async (tab: string) => {
  window.history.replaceState(null, '', `/settings?tab=${tab}`);
  render(<Settings />);
  expect(await screen.findByRole('tab', { name: tab === 'network' ? 'Network' : 'Roof', selected: true })).toBeInTheDocument();
};

describe('Ethernet in Settings and WiFi setup', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('a DevKit has no Ethernet card and no network mode', async () => {
    mockBoard('devkit');
    await openTab('network');
    await waitFor(() => expect(document.getElementById('wifi')).not.toBeNull());
    expect(document.getElementById('ethernet')).toBeNull();
    expect(document.querySelector('[data-field="network.mode"]')).toBeNull();
    expect(screen.queryByText(/WiFi is optional/)).toBeNull();
  });

  it('a WT32-ETH01 shows the link, its addresses and the network mode', async () => {
    mockBoard('wt32');
    await openTab('network');
    const card = await waitFor(() => {
      const el = document.getElementById('ethernet');
      if (!el) throw new Error('no Ethernet card yet');
      return within(el);
    });
    expect(card.getByText('Connected')).toBeInTheDocument();
    expect(card.getByText('192.168.1.60')).toBeInTheDocument();
    expect(card.getByText('A1:B2:C3:D4:E5:F7')).toBeInTheDocument();
    expect(card.getByText('100 Mbit/s')).toBeInTheDocument();
    const mode = document.querySelector<HTMLSelectElement>('[data-field="network.mode"]')!;
    expect(mode.value).toBe('auto');
    expect(card.getByText('Ethernet, with saved WiFi taking over after 30 s without an address.')).toBeInTheDocument();
    fireEvent.change(mode, { target: { value: 'ethernet' } });
    expect(await card.findByText('Ethernet only: WiFi and the setup hotspot stay off.')).toBeInTheDocument();
    // The mode in effect is still Auto until a restart: WiFi isn't off yet, and Scan works.
    expect(screen.queryByText('WiFi is off: the network mode is Ethernet only.')).toBeNull();
    expect(screen.getByRole('button', { name: 'Scan' })).toBeEnabled();
  });

  it('in Ethernet-only mode (in effect) WiFi is off and Scan says why', async () => {
    mockBoard('wt32', { network: { mode: 'ethernet' } });
    await openTab('network');
    const note = await screen.findByText('WiFi is off: the network mode is Ethernet only.');
    const scan = screen.getByRole('button', { name: 'Scan' });
    expect(scan).toBeDisabled();
    expect(scan).toHaveAttribute('aria-describedby', note.id);
  });

  it('in WiFi-only mode the Ethernet card stays, Off rather than No link, so the mode can change back', async () => {
    mockBoard('wt32', { network: { mode: 'wifi' } });
    await openTab('network');
    const card = await waitFor(() => {
      const el = document.getElementById('ethernet');
      if (!el) throw new Error('no Ethernet card yet');
      return within(el);
    });
    expect(card.getByText('Off (WiFi only)')).toHaveClass('pill-dim');
    expect(card.queryByText('No link')).toBeNull();
    expect(card.queryByText('192.168.1.60')).toBeNull();
    expect(document.querySelector<HTMLSelectElement>('[data-field="network.mode"]')!.value).toBe('wifi');
  });

  it('on Ethernet an empty WiFi network is optional, not an error', async () => {
    mockBoard('wt32', { wifi: { ...mockConfig.wifi, ssid: '' } });
    await openTab('network');
    expect(await screen.findByText('WiFi is optional on this board: Ethernet is the main link.')).toBeInTheDocument();
    fireEvent.change(document.querySelector('[data-field="network.mode"]')!, { target: { value: 'wifi' } });
    await waitFor(() => expect(screen.queryByText("WiFi network can't be blank; use WiFi setup to change networks")).toBeNull());
  });

  it('Settings → Roof has the board and pins card with its preset', async () => {
    mockBoard('unset');
    await openTab('roof');
    const card = await waitFor(() => {
      const el = document.getElementById('pins');
      if (!el) throw new Error('no pins card yet');
      return within(el);
    });
    expect(card.getByText('Not chosen')).toBeInTheDocument();
    expect(card.getByText('No board is chosen yet, so nothing is driven: choose one, then restart.')).toBeInTheDocument();
    expect(card.getByRole('button', { name: 'Open the pins page' })).toBeInTheDocument();
  });

  it('the WiFi setup page says WiFi is optional on Ethernet, and off in Ethernet-only mode', async () => {
    mockBoard('wt32');
    const { unmount } = render(<WifiSetup />);
    expect(await screen.findByText('This device is on Ethernet at 192.168.1.60, so WiFi is optional.')).toBeInTheDocument();
    unmount();
    mockBoard('wt32', { network: { mode: 'ethernet' } });
    server.use(
      http.get('/api/wifi/scan', () => HttpResponse.json({ error: 'Wi-Fi is off: the network mode is Ethernet only' }, { status: 409 })),
    );
    render(<WifiSetup />);
    const note = await screen.findByText('WiFi is off: the network mode is Ethernet only (Settings, Network).');
    // Scan and Connect are off with the reason; no scan is asked for (the device would refuse it).
    expect(screen.getByRole('button', { name: 'Scan again' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Scan again' })).toHaveAttribute('aria-describedby', note.id);
    expect(screen.getByRole('button', { name: 'Connect' })).toBeDisabled();
    expect(screen.queryByText('Wi-Fi is off: the network mode is Ethernet only')).toBeNull();
  });

  it('the WiFi setup page says nothing new on a DevKit', async () => {
    mockBoard('devkit');
    render(<WifiSetup />);
    await screen.findByRole('radio', { name: /DarkSkyLab/ });
    expect(screen.queryByText(/Ethernet/)).toBeNull();
  });
});
