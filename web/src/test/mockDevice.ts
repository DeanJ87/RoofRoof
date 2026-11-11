import { http, HttpResponse } from 'msw';
import { mockConfig } from '../mocks/data';
import { evaluate, type DepFacts } from '../lib/settingsDeps';
import type { Config } from '../types';
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
