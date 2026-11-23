import { beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Pins from '../Pins';
import { Toaster } from '../toast';
import type { Config, SystemStatus } from '../../types';
import { mockConfig } from '../../mocks/config';
import { mockStatus } from '../../mocks/data';
import { mockWiring } from '../../mocks/pins';
import { mockBoard } from '../../test/mockDevice';
import { server } from '../../test/mswServer';
import { restartCodes } from '../settings/restart';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 023 Advanced: the Pins page's analog front end (presets, dividers and
// trims, the device's checks, calibrating the supply) and the park pull-ups,
// saved with the pins in one settings save.

const live = (status: SystemStatus = mockStatus) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

const boardRadio = (name: string | RegExp) => within(screen.getByRole('radiogroup', { name: 'Board preset' })).getByRole('radio', { name });
const analogRadio = (name: string | RegExp) =>
  within(screen.getByRole('radiogroup', { name: 'Analog front end' })).getByRole('radio', { name });
const button = (name: string) => screen.getByRole('button', { name });
const field = (name: string) => document.querySelector<HTMLInputElement>(`[data-field="${name}"]`);
const advanced = () => within(document.querySelector('[data-field="pins.advanced"]') as HTMLElement);
const inputRows = (input: string) => within(document.querySelector(`[data-analog-input="${input}"]`) as HTMLElement);
const row = (scope: ReturnType<typeof inputRows>, label: string | RegExp) => scope.getByText(label).closest('.reading-row');

const type = (name: string, value: string) => {
  const input = field(name)!;
  fireEvent.input(input, { target: { value } });
  fireEvent.change(input, { target: { value } });
};

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

const IS_WARNING = 'R_IS divider: at its highest (5.0 V) the input would put 3.33 V on the pin, over 3.3 V';

const openPage = async () => {
  render(<Toaster />);
  render(<Pins />);
  await screen.findByRole('radiogroup', { name: 'Analog front end' });
};

const chooseCustom = async () => {
  fireEvent.click(analogRadio(/^Custom/));
  await waitFor(() => expect(field('analog.supply.top')).not.toBeNull());
};

describe('Pins page: Advanced', () => {
  beforeEach(() => {
    live();
    mockWiring.state = { state: 'idle', checks: [] };
  });

  it('is a fold, closed at first; a named preset shows its values read-only with the DevKit IS warnings', async () => {
    mockBoard('devkit');
    await openPage();
    const fold = document.querySelector<HTMLDetailsElement>('details[data-field="pins.advanced"]')!;
    expect(fold.open).toBe(false);
    expect(analogRadio(/^DevKit bench wiring/)).toBeChecked();
    expect(field('analog.supply.top')).toBeNull();
    const supply = inputRows('supply');
    expect(row(supply, 'Top resistor')).toHaveTextContent('100 kΩ');
    expect(row(supply, 'Bottom resistor')).toHaveTextContent('22 kΩ');
    expect(row(supply, 'Divider ratio')).toHaveTextContent('x5.545');
    expect(row(supply, 'Pin voltage at 15.0 V in')).toHaveTextContent('2.70 V');
    expect(row(inputRows('currentR'), 'Pin voltage at 5.0 V in')).toHaveTextContent('3.33 V');
    // Hall has no trim.
    expect(inputRows('hall').queryByText('Gain')).toBeNull();
    const warning = await advanced().findByText(IS_WARNING);
    expect(warning.closest('[data-analog-issue]')).toHaveAttribute('data-level', 'warning');
    expect(document.querySelector('[data-analog-issue="currentL"][data-level="warning"]')).not.toBeNull();
    // Nothing changed: no Save.
    expect(screen.queryByRole('button', { name: 'Save pins' })).toBeNull();
  });

  it('Custom: an error from the device blocks Save, opens the fold and points at the input; a warning is allowed', async () => {
    mockBoard('devkit');
    await openPage();
    await chooseCustom();
    type('analog.supply.top', '50');
    const error = await screen.findByText('Supply sense divider: each resistor must be between 100 Ohm and 10 MOhm');
    expect(error.closest('[data-analog-issue]')).toHaveAttribute('data-level', 'error');
    expect(error.closest('[role="status"]')).not.toBeNull();
    await waitFor(() => expect(button('Save pins')).toBeDisabled());
    expect(screen.getByText('Fix the errors above to save.')).toBeInTheDocument();
    expect(field('analog.supply.top')).toHaveAttribute('aria-invalid', 'true');
    expect(field('analog.supply.top')!.getAttribute('aria-describedby')).toContain(error.closest('p')!.id);
    expect(document.querySelector<HTMLDetailsElement>('details[data-field="pins.advanced"]')!.open).toBe(true);
    type('analog.supply.top', '100000');
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    expect(screen.getByText(IS_WARNING)).toBeInTheDocument();
  });

  it('a bench 10k/22k on R_IS saves (its warning allowed) and applies without a restart', async () => {
    mockBoard('devkit');
    const saves = recordSaves();
    await openPage();
    await chooseCustom();
    type('analog.currentR.bottom', '22000');
    expect(await screen.findByText(/^R_IS divider: at its highest \(5\.0 V\) the input would put 3\.44 V/)).toBeInTheDocument();
    expect(row(inputRows('currentR'), 'Divider ratio')).toHaveTextContent('x1.455');
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    fireEvent.click(button('Save pins'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0]).toEqual({ analog: { preset: 'custom', currentR: { bottom: 22000 } } });
    expect(await screen.findByText('Analog settings saved. They apply once the roof is still.')).toBeInTheDocument();
  });

  it('a Custom analog setup stays when a board preset is chosen', async () => {
    mockBoard('devkit');
    const saves = recordSaves();
    await openPage();
    await chooseCustom();
    fireEvent.click(boardRadio(/^WT32-ETH01/));
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    expect(analogRadio(/^Custom/)).toBeChecked();
    fireEvent.click(button('Save pins'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].analog).toEqual({ preset: 'custom' });
  });

  it('the analog preset never follows the board preset (pins apply at a restart, a carrier v1 has no diode)', async () => {
    mockBoard('wt32');
    await openPage();
    expect(analogRadio(/^WT32 carrier v1\.1/)).toBeChecked();
    expect(row(inputRows('supply'), 'Offset')).toHaveTextContent('0.30 V');
    fireEvent.click(boardRadio(/^ESP32 DevKit/));
    expect(analogRadio(/^WT32 carrier v1\.1/)).toBeChecked();
  });

  it('choosing the WT32 board leaves the DevKit analog preset (no +0.3 V for a carrier without the diode)', async () => {
    mockBoard('devkit');
    await openPage();
    fireEvent.click(boardRadio(/^WT32-ETH01/));
    expect(analogRadio(/^DevKit bench wiring/)).toBeChecked();
  });

  it('calibrates the supply to a measured voltage: the gain set, the preset Custom, to save', async () => {
    mockBoard('devkit');
    const saves = recordSaves();
    await openPage();
    expect(row(within(document.querySelector('[data-analog-calibrate]') as HTMLElement), 'Supply reading now')).toHaveTextContent(
      '12.46 V',
    );
    type('analog.calibrate.measured', '12.8');
    fireEvent.click(button('Set the gain'));
    expect(await screen.findByText('Gain set to 1.0273, as Custom. Save to keep it.')).toBeInTheDocument();
    expect(analogRadio(/^Custom/)).toBeChecked();
    expect(field('analog.supply.gain')!.value).toBe('1.0273');
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    fireEvent.click(button('Save pins'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0].analog).toEqual({ preset: 'custom', supply: { gain: 1.0273 } });
  });

  // The reading was scaled with the saved setup: an unsaved +0.3 V (the WT32 preset) is made room for, not added twice.
  it('calibrates from the saved setup while the analog setup is edited', async () => {
    mockBoard('devkit');
    await openPage();
    fireEvent.click(analogRadio(/^WT32 carrier v1\.1/));
    type('analog.calibrate.measured', '12.46');
    fireEvent.click(button('Set the gain'));
    // (12.46 - 0.3) / 12.46
    expect(await screen.findByText('Gain set to 0.9759, as Custom. Save to keep it.')).toBeInTheDocument();
    expect(field('analog.supply.offsetVolts')!.value).toBe('0.3');
  });

  it('refuses to calibrate with no supply reading', async () => {
    mockBoard('devkit');
    live({ ...mockStatus, roof: { ...mockStatus.roof!, supplyVolts: null } });
    await openPage();
    type('analog.calibrate.measured', '12.8');
    fireEvent.click(button('Set the gain'));
    expect(await screen.findByText('There is no supply reading of 1 V or more to calibrate from.')).toBeInTheDocument();
    expect(analogRadio(/^DevKit bench wiring/)).toBeChecked();
  });

  it('a park pull-up choice is saved with the pins and needs a restart', async () => {
    mockBoard('devkit');
    const saves = recordSaves();
    await openPage();
    expect(document.querySelector<HTMLSelectElement>('[data-field="pins.parkAPullup"]')!.value).toBe('internal');
    fireEvent.change(document.querySelector('[data-field="pins.parkAPullup"]')!, { target: { value: 'external' } });
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    fireEvent.click(button('Save pins'));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(saves[0]).toEqual({ pins: { parkAPullup: 'external' } });
    expect(await screen.findByText('Pins saved. They apply at the next restart.')).toBeInTheDocument();
  });

  it('a park switch on GPIO34-39 needs an external pull-up', async () => {
    mockBoard('devkit');
    await openPage();
    fireEvent.click(boardRadio(/^Custom/));
    await waitFor(() => expect(field('pins.parkA')).not.toBeNull());
    type('pins.parkA', '37');
    expect(await screen.findByText('Park switch A on GPIO37: GPIO37 has no internal pull-up')).toBeInTheDocument();
    await waitFor(() => expect(button('Save pins')).toBeDisabled());
    fireEvent.change(document.querySelector('[data-field="pins.parkAPullup"]')!, { target: { value: 'external' } });
    await waitFor(() => expect(button('Save pins')).toBeEnabled());
    expect(screen.queryByText('Park switch A on GPIO37: GPIO37 has no internal pull-up')).toBeNull();
  });
});

describe('what applies when (restart codes)', () => {
  const before = mockConfig as Config;
  it('an analog change needs no restart; a pull-up change is a pins change', () => {
    const analog = { ...before.analog!, preset: 'custom' as const, supply: { ...before.analog!.supply, gain: 1.02 } };
    expect(restartCodes(before, { ...before, analog })).toEqual([]);
    expect(restartCodes(before, { ...before, pins: { ...before.pins!, parkBPullup: 'external' } })).toEqual(['pins']);
  });
});
