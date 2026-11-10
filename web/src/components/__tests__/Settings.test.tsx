import { describe, it, expect, vi, afterEach } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { mockConfig } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';

const withConfig = (overrides: Record<string, unknown>) => mockDevice({ config: overrides });

describe('Settings', () => {
  afterEach(() => {
    window.history.replaceState(null, '', '/');
  });

  it('opens the Network tab from the Alpaca setup link and scrolls to Alpaca', async () => {
    const scrollIntoView = vi.fn();
    Element.prototype.scrollIntoView = scrollIntoView;
    window.history.replaceState(null, '', '/settings?section=alpaca');

    const { container } = render(<Settings />);

    expect(await screen.findByRole('tab', { name: 'Network', selected: true })).toBeInTheDocument();
    await waitFor(() => expect(scrollIntoView).toHaveBeenCalled());
    expect(scrollIntoView.mock.contexts[0]).toBe(container.querySelector('#alpaca'));
  });

  it('shows an error under its own field', async () => {
    withConfig({ mqtt: { ...mockConfig.mqtt, enabled: true, topic: 'roofroof', broker: 'mqtt.example.com' } });
    window.history.replaceState(null, '', '/settings?tab=network');
    render(<Settings />);
    const [broker] = await screen.findAllByLabelText('Broker', { selector: 'input' });
    fireEvent.input(broker, { target: { value: '' } });
    const message = await screen.findByText('MQTT broker and topic are required when MQTT is enabled');
    expect(message.closest('.field')?.querySelector('.field-label')?.textContent).toContain('Broker');
  });

  it('switches tabs and records the tab in the URL', async () => {
    render(<Settings />);
    fireEvent.click(await screen.findByRole('tab', { name: 'Alerts' }));
    expect(screen.getByRole('tab', { name: 'Alerts', selected: true })).toBeInTheDocument();
    expect(window.location.search).toBe('?tab=alerts');
    expect(await screen.findByText('Notify me when')).toBeInTheDocument();
  });

  it('shows the save bar only with unsaved changes, and blocks alert tests until saved', async () => {
    window.history.replaceState(null, '', '/settings?tab=alerts');
    render(<Settings />);

    const pushoverTest = async () => (await screen.findAllByText('Send test'))[0];
    expect(await pushoverTest()).not.toBeDisabled();
    expect(screen.queryByText('Unsaved changes')).toBeNull();

    fireEvent.click(screen.getByLabelText('ntfy'));
    expect(screen.getByText('Unsaved changes')).toBeInTheDocument();
    expect(await pushoverTest()).toBeDisabled();

    fireEvent.click(screen.getByText('Discard'));
    expect(screen.queryByText('Unsaved changes')).toBeNull();
  });

  it('offers a restart when a saved change only applies after one', async () => {
    window.history.replaceState(null, '', '/settings?tab=network');
    render(<Toaster />);
    render(<Settings />);

    fireEvent.click(await screen.findByLabelText('Serve Alpaca'));
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));

    expect(await screen.findByText('Saved. Restart to apply Alpaca discovery.')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Restart' })).toBeInTheDocument();
  });

  it('offers a restart after switching IPv6 off (spec 015)', async () => {
    window.history.replaceState(null, '', '/settings?tab=network');
    render(<Toaster />);
    render(<Settings />);

    fireEvent.click(await screen.findByLabelText('IPv6'));
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));

    expect(await screen.findByText('Saved. Restart to apply IPv6.')).toBeInTheDocument();
  });

  it('checks the MQTT broker with the device rules, IPv6 included', async () => {
    window.history.replaceState(null, '', '/settings?tab=network');
    withConfig({ mqtt: { ...mockConfig.mqtt, enabled: true, broker: 'broker.local' } });
    render(<Settings />);

    const broker = await screen.findByDisplayValue('broker.local');
    fireEvent.input(broker, { target: { value: 'broker.local:1883' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(await screen.findByText('Put the port in the Port field')).toBeInTheDocument();

    fireEvent.input(broker, { target: { value: '[fd00::10]:1883' } });
    await waitFor(() => expect(screen.queryByText('Put the port in the Port field')).toBeNull());
  });

  it('just confirms saves that apply immediately', async () => {
    window.history.replaceState(null, '', '/settings?tab=alerts');
    render(<Toaster />);
    render(<Settings />);

    fireEvent.click(await screen.findByLabelText('ntfy'));
    fireEvent.input(screen.getByDisplayValue('https://ntfy.sh').closest('.card-group')!.querySelector('[name="alerts.ntfy.topic"]')!, {
      target: { value: 'my-topic' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));

    expect(await screen.findByText('Saved.')).toBeInTheDocument();
  });

  it('jumps to the tab holding the first validation error on save', async () => {
    window.history.replaceState(null, '', '/settings?tab=device');
    withConfig({ alerts: { ...mockConfig.alerts, ntfy: { enabled: true, server: 'https://ntfy.sh', topic: 'x', token: '' } } });
    render(<Settings />);
    fireEvent.click(await screen.findByRole('tab', { name: 'Alerts' }));
    fireEvent.input(screen.getByDisplayValue('x'), { target: { value: '' } });
    fireEvent.click(screen.getByRole('tab', { name: 'Device' }));
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));

    expect(await screen.findByRole('tab', { name: 'Alerts', selected: true })).toBeInTheDocument();
    expect(screen.getByText('Topic is required')).toBeInTheDocument();
  });
});
