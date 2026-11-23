import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Pins from '../Pins';
import { Toaster } from '../toast';
import type { SystemStatus } from '../../types';
import { mockStatus } from '../../mocks/data';
import { mockWiring, mockWiringDone, pinsDocumentFor } from '../../mocks/pins';
import { mockBoard } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 023 FR-026: the Pins page. The preset as choice cards, the pins
// (editable for Custom) with the device's errors and warnings, the pins in
// effect with Restart, and the wiring check.

const live = (status: SystemStatus = mockStatus) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

const presets = async () => screen.findByRole('radiogroup', { name: 'Board preset' });
const radio = (name: string | RegExp) => screen.getByRole('radio', { name });
const boardCard = () => within(document.getElementById('pins')!);
const inEffect = () => within(document.getElementById('pins-in-effect')!);
const button = (name: string) => screen.getByRole('button', { name });

// Every POST /api/config body, in order.
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

const chooseCustom = async () => {
  await presets();
  fireEvent.click(radio('Custom'));
  return waitFor(() => expect(document.querySelector('[data-field="pins.rpwm"]')).not.toBeNull());
};

const typePin = (fn: string, value: string) => {
  const input = document.querySelector<HTMLInputElement>(`[data-field="pins.${fn}"]`)!;
  fireEvent.input(input, { target: { value } });
  fireEvent.change(input, { target: { value } });
};

describe('Pins page', () => {
  beforeEach(() => {
    live();
    mockWiring.state = { state: 'idle', checks: [] };
  });
  afterEach(() => vi.useRealTimers());

  it('shows a DevKit with its preset chosen, its pins read-only and in effect', async () => {
    mockBoard('devkit');
    render(<Pins />);
    await presets();
    expect(radio(/^ESP32 DevKit/)).toBeChecked();
    expect(document.querySelector('[data-field="pins.rpwm"]')).toBeNull(); // read-only for a named preset
    expect(boardCard().getByText('RPWM').closest('.reading-row')).toHaveTextContent('GPIO25');
    expect(inEffect().getByText('Valid')).toBeInTheDocument();
    expect(document.querySelector('[data-pins-restart]')).toBeNull();
    expect(screen.queryByText(/No board is chosen yet/)).toBeNull();
  });

  it('on a new device says nothing is driven and chooses the WT32 its probe found, to save', async () => {
    mockBoard('unset');
    render(<Pins />);
    await presets();
    expect(screen.getByText('No board is chosen yet, so nothing is driven: choose one, then restart.')).toBeInTheDocument();
    expect(screen.getByText('A WT32-ETH01 was found, so its preset is chosen.')).toBeInTheDocument();
    await waitFor(() => expect(radio(/^WT32-ETH01 \+ RoofRoof carrier v1/)).toBeChecked());
    expect(radio(/^WT32-ETH01 \+ RoofRoof carrier v1/)).toHaveAccessibleDescription(/Suggested/);
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    // The wiring check needs a board in effect.
    const check = button('Check wiring');
    expect(check).toBeDisabled();
    expect(document.getElementById(check.getAttribute('aria-describedby')!)).toHaveTextContent(
      'Choose a board and restart before checking the wiring.',
    );
  });

  it('on a new device with nothing found, chooses the DevKit', async () => {
    mockBoard('unset');
    server.use(http.get('/api/pins', () => HttpResponse.json({ ...pinsDocumentFor('unset'), detected: null })));
    render(<Pins />);
    await presets();
    expect(screen.getByText('No Ethernet chip was found, so the ESP32 DevKit is chosen.')).toBeInTheDocument();
    await waitFor(() => expect(radio(/^ESP32 DevKit/)).toBeChecked());
  });

  it('shows a WT32-ETH01 on its carrier with its pins', async () => {
    mockBoard('wt32');
    render(<Pins />);
    await presets();
    expect(radio(/^WT32-ETH01 \+ RoofRoof carrier v1/)).toBeChecked();
    expect(boardCard().getByText('Board').closest('.reading-row')).toHaveTextContent('WT32-ETH01');
    expect(boardCard().getByText('EN (R_EN and L_EN)').closest('.reading-row')).toHaveTextContent('GPIO4');
    // Its strapping-pin warnings are allowed.
    await waitFor(() => expect(document.querySelector('[data-pin-issue="parkB"][data-level="warning"]')).not.toBeNull());
  });

  it('Custom: an error blocks Save with the device text; a warning is shown and allowed', async () => {
    mockBoard('devkit');
    render(<Pins />);
    await chooseCustom();
    typePin('rpwm', '35');
    const error = await screen.findByText("RPWM on GPIO35: GPIO35 is input only and can't drive an output");
    expect(error.closest('[data-pin-issue]')).toHaveAttribute('data-level', 'error');
    await waitFor(() => expect(button('Save pins')).toBeDisabled());
    expect(screen.getByText('Fix the errors above to save.')).toBeInTheDocument();
    typePin('rpwm', '25');
    typePin('lpwm', '15');
    const warning = await screen.findByText(/^LPWM on GPIO15: a strapping pin/);
    expect(warning.closest('[data-pin-issue]')).toHaveAttribute('data-level', 'warning');
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
  });

  it('saves through the settings save and says a restart applies it', async () => {
    mockBoard('devkit');
    const saves = recordSaves();
    render(<Toaster />);
    render(<Pins />);
    await presets();
    fireEvent.click(radio(/^WT32-ETH01/));
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    server.use(
      http.get('/api/pins', () =>
        HttpResponse.json({ ...pinsDocumentFor('devkit'), saved: pinsDocumentFor('wt32').saved, restartNeeded: true }),
      ),
    );
    fireEvent.click(button('Save pins'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].pins).toMatchObject({ preset: 'wt32-carrier-v1', board: 'wt32-eth01', rpwm: 14 });
    expect(await screen.findByText('Pins saved. They apply at the next restart.')).toBeInTheDocument();
    expect(await inEffect().findByText('The saved pins apply at a restart.')).toBeInTheDocument();
    expect(inEffect().getByText('RPWM').closest('.reading-row')).toHaveTextContent('GPIO25, then GPIO14');
    expect(inEffect().getByRole('button', { name: 'Restart now' })).toBeEnabled();
  });

  it('shows the device refusal of a save while the roof is busy', async () => {
    mockBoard('devkit');
    server.use(http.post('/api/config', () => HttpResponse.json({ error: 'The roof is moving: wait until it stops' }, { status: 409 })));
    render(<Pins />);
    await presets();
    fireEvent.click(radio(/^WT32-ETH01/));
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    fireEvent.click(button('Save pins'));
    expect(await screen.findByText('The roof is moving: wait until it stops')).toBeInTheDocument();
  });

  it('runs the wiring check and lists each result with its level, value and text', async () => {
    mockBoard('devkit');
    render(<Pins />);
    await presets();
    expect(await screen.findByText("The wiring hasn't been checked since the last restart.")).toBeInTheDocument();
    fireEvent.click(button('Check wiring'));
    const supply = await waitFor(() => {
      const row = document.querySelector('[data-wiring-check="supply"]');
      if (!row) throw new Error('no results yet');
      return within(row as HTMLElement);
    });
    expect(supply.getByText('Pass')).toBeInTheDocument();
    expect(supply.getByText(/12\.4 V/, { selector: '.mono' })).toBeInTheDocument();
    expect(supply.getByText('12.4 V: a plausible supply')).toBeInTheDocument();
    expect(within(document.querySelector('[data-wiring-check="currentR"]') as HTMLElement).getByText('Check')).toBeInTheDocument();
    expect(screen.getByText('Direction and travel need the roof to move.')).toBeInTheDocument();
    expect(button('Open Jog and teach')).toBeInTheDocument();
  });

  it('offers the WT32 preset when the check found Ethernet on a DevKit setup', async () => {
    mockBoard('devkit');
    mockWiring.state = { ...mockWiringDone, suggestPreset: 'wt32-carrier-v1' };
    render(<Pins />);
    fireEvent.click(await screen.findByRole('button', { name: 'Use that preset' }));
    await waitFor(() => expect(radio(/^WT32-ETH01/)).toBeChecked());
  });

  it('a refused check shows the device reason', async () => {
    mockBoard('devkit');
    server.use(http.post('/api/pins/check', () => HttpResponse.json({ error: 'A check is already running' }, { status: 409 })));
    render(<Pins />);
    await presets();
    fireEvent.click(button('Check wiring'));
    expect(await screen.findByText('A check is already running')).toBeInTheDocument();
  });
});
