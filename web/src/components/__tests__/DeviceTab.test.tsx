import { describe, it, expect } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/preact';
import Settings from '../Settings';
import { mockDevice } from '../../test/mockDevice';

const openDevice = () => {
  window.history.replaceState(null, '', '/settings?tab=device');
  render(<Settings />);
};

describe('Device tab', () => {
  it('shows the device name', async () => {
    mockDevice({ config: { deviceName: 'Garden roof' } });
    openDevice();
    expect(await screen.findByDisplayValue('Garden roof')).toBeInTheDocument();
  });

  it('asks for credentials only once protection is on', async () => {
    mockDevice();
    openDevice();
    const protect = await screen.findByRole('switch', { name: /Password-protect changes/ });
    expect(screen.queryByLabelText('Username')).toBeNull();
    fireEvent.click(protect);
    expect(await screen.findByLabelText('Username')).toBeInTheDocument();
  });

  it('asks for an upload password once ArduinoOTA uploads are on', async () => {
    mockDevice();
    openDevice();
    fireEvent.click(await screen.findByRole('switch', { name: /ArduinoOTA uploads/ }));
    expect(await screen.findByLabelText('Upload password')).toBeInTheDocument();
  });
});
