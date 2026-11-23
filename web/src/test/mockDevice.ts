import { http, HttpResponse } from 'msw';
import { mockConfig, mockStatus } from '../mocks/data';
import { ethernetFor, mockValidate, pinsDocumentFor, type MockBoard } from '../mocks/pins';
import { evaluate, type DepFacts } from '../lib/settingsDeps';
import type { Config, SystemStatus } from '../types';
import { server } from './mswServer';

// A healthy device. It offers one Alpaca device, so the imaging-app alerts
// are live; D-37's case without one sets alpacaDevices: 0.
export const healthyFacts: DepFacts = {
  wifiConnected: true,
  mqttConnected: true,
  clockSet: true,
  alpacaDevices: 1,
};

/**
 * Serves a device whose settings are `mockConfig` with `config` on top, and
 * whose settings-dependency report (GET /api/settings/effective) agrees with
 * them, as a real device's would.
 */
export const mockDevice = ({
  config = {},
  facts = {},
}: { config?: Partial<Config> | Record<string, unknown>; facts?: Partial<DepFacts> } = {}) => {
  const merged = { ...mockConfig, ...config } as Config;
  const deviceFacts = { ...healthyFacts, ...facts };
  server.use(
    http.get('/api/config', () => HttpResponse.json(merged)),
    http.get('/api/settings/effective', () =>
      HttpResponse.json({
        facts: deviceFacts,
        settings: evaluate(merged, deviceFacts).map(({ blockedBy: _blockedBy, ...entry }) => entry),
      }),
    ),
  );
  return merged;
};

/**
 * Serves a device on `kind` of board (spec 023): GET /api/pins, POST /api/pins/validate,
 * the status's `pins` and `ethernet` blocks and the settings' `pins`. `wt32` is a WT32-ETH01
 * on its carrier with Ethernet up in the saved network mode (`config.network`, in effect
 * since boot); `unset` a new device whose boot probe found one (Ethernet up, a map for
 * another board refused as `boardDetected`). The analog setup is the board's preset's
 * (the DevKit's two IS warnings included); validation checks an `analog` sent with the pins.
 */
export const mockBoard = (kind: MockBoard, config: Partial<Config> = {}) => {
  const document = pinsDocumentFor(kind);
  const mode = kind === 'devkit' ? 'wifi' : (config.network?.mode ?? 'auto');
  const status: SystemStatus = {
    ...mockStatus,
    ethernet: ethernetFor(document.board, mode),
    pins: { preset: document.running.preset, board: document.board, valid: document.valid, restartNeeded: false },
  };
  // The device's evidence it is a WT32-ETH01: its boot probe, or Ethernet running.
  const detected = document.detected ?? (status.ethernet?.started ? 'wt32-eth01' : null);
  server.use(
    http.get('/api/pins', () => HttpResponse.json(document)),
    http.get('/api/status', () => HttpResponse.json(status)),
    http.post('/api/pins/validate', async ({ request }) =>
      HttpResponse.json(mockValidate((await request.json()) as Parameters<typeof mockValidate>[0], detected, document.analog)),
    ),
  );
  mockDevice({ config: { pins: document.saved, analog: document.analog, ...config } });
  return { document, status };
};
