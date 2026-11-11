import { describe, it, expect } from 'vitest';
import { http, HttpResponse } from 'msw';
import { render, screen } from '@testing-library/preact';
import { server } from '../../test/mswServer';
import { mockStatus } from '../../mocks/data';
import Alpaca from '../Alpaca';
import { describeClient } from '../../lib/alpacaClients';

const dome = {
  DeviceName: 'RoofRoof Dome',
  DeviceType: 'Dome',
  DeviceNumber: 0,
  UniqueID: 'roofroof-dome-0',
};
const envelope = <T,>(Value: T) => ({ Value, ErrorNumber: 0, ErrorMessage: '', ClientTransactionID: 0, ServerTransactionID: 1 });

describe('Alpaca', () => {
  it('says there is no device yet while none is configured', async () => {
    render(<Alpaca />);
    expect(await screen.findByText(/No Alpaca device yet/)).toBeInTheDocument();
    expect(screen.getByText(`${window.location.origin}/management/v1/configureddevices`)).toBeInTheDocument();
  });

  it('lists a configured device with its setup URL and live device state', async () => {
    server.use(
      http.get('/management/v1/configureddevices', () => HttpResponse.json(envelope([dome]))),
      http.get('/api/v1/dome/0/devicestate', () => HttpResponse.json(envelope([{ Name: 'ShutterStatus', Value: 0 }]))),
    );
    render(<Alpaca />);

    expect(await screen.findByText('RoofRoof Dome')).toBeInTheDocument();
    expect(screen.getByText(`${window.location.origin}/setup/v1/dome/0/setup`)).toBeInTheDocument();
    expect(await screen.findByText('ShutterStatus')).toBeInTheDocument();
  });
});

describe('Alpaca imaging app state (SQMeter spec 021, 026 DS-21)', () => {
  it('shows whether an imaging app is checking each device: a pill and when it last checked', async () => {
    const client = { connected: true, watching: true, silent: false, lastCheckedAgeMs: 2000, clientId: 7 };
    server.use(
      http.get('/management/v1/configureddevices', () => HttpResponse.json(envelope([dome]))),
      http.get('/api/status', () => HttpResponse.json({ ...mockStatus, alpaca: { enabled: true, clients: { dome: client } } })),
    );
    render(<Alpaca />);
    expect(await screen.findByText('Checked 2 s ago')).toBeInTheDocument();
    expect(screen.getByText('Connected')).toBeInTheDocument();
  });

  it('describes every state', () => {
    const base = { connected: false, watching: false, silent: false, lastCheckedAgeMs: null, clientId: null };
    expect(describeClient({ ...base, connected: true, watching: true, lastCheckedAgeMs: 3000 })).toEqual({
      state: 'Connected',
      checked: 'Checked 3 s ago',
      tone: 'ok',
    });
    expect(describeClient({ ...base, connected: true, watching: true, silent: true, lastCheckedAgeMs: 180000 })).toEqual({
      state: 'Gone quiet',
      checked: 'Checked 3 min ago',
      tone: 'warn',
    });
    expect(describeClient({ ...base, watching: true, lastCheckedAgeMs: 1000 }).state).toBe('Checking');
    expect(describeClient({ ...base, lastCheckedAgeMs: 7200000 })).toMatchObject({
      state: 'Disconnected',
      checked: 'Checked 2 h 0 min ago',
    });
    expect(describeClient(base)).toEqual({ state: 'Waiting', tone: 'muted' });
  });
});
