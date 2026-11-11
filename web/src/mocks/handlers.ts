import { http, HttpResponse, ws } from 'msw';
import type { AlertScheduleReason } from '../types';
import { mockStatus, mockConfig, mockInputs, mockWifiNetworks, mockAlpacaDevices, mockRecentAlerts, mockCalibration } from './data';
import { evaluate, type DepFacts } from '../lib/settingsDeps';

// What a healthy device would report as its facts.
const mockFacts: DepFacts = {
  wifiConnected: true,
  mqttConnected: mockStatus.mqtt?.connected ?? false,
  clockSet: true,
  alpacaDevices: 1, // the roof's Dome (spec 006)
};

// Demo-only: alerts switched on/off.
const mockAlertsArmed: { value: boolean; reason: AlertScheduleReason; since: string | null } = { value: true, reason: 'none', since: null };
const mockSchedule = () => ({
  armed: mockAlertsArmed.value,
  mode: 'any' as const,
  reason: mockAlertsArmed.reason as AlertScheduleReason,
  since: mockAlertsArmed.since,
  sinceAgeMs: mockAlertsArmed.since ? Date.now() - Date.parse(mockAlertsArmed.since) : null,
});
const setMockArmed = (request: Request, armed: boolean) => {
  mockAlertsArmed.value = armed;
  mockAlertsArmed.reason = new URL(request.url).searchParams.get('source') === 'ui' ? 'user-ui' : 'user-rest';
  mockAlertsArmed.since = new Date().toISOString();
};

const alpacaEnvelope = <T>(Value: T) => ({
  Value,
  ClientTransactionID: 0,
  ServerTransactionID: 1,
  ErrorNumber: 0,
  ErrorMessage: '',
});

// WebSocket handlers — wildcard host works on any host
// The demo "booted" when the page loaded, mockStatus.uptime seconds ago.
const demoStartedAt = Date.now();
const demoUptime = () => mockStatus.uptime + Math.floor((Date.now() - demoStartedAt) / 1000);

const statusSocket = ws.link('*/ws/status');

export const handlers = [
  // REST — system status (refresh uptime each call)
  http.get('/api/status', () =>
    HttpResponse.json({
      ...mockStatus,
      alerts: mockSchedule(),
      uptime: demoUptime(),
      time: { iso: new Date().toISOString(), timezone: 'GMT0' },
    }),
  ),

  // ASCOM Alpaca — management + device state
  http.get('/management/v1/configureddevices', () => HttpResponse.json(alpacaEnvelope(mockAlpacaDevices))),
  // The Dome closed and still (ShutterStatus 1 = closed).
  http.get('/api/v1/dome/0/devicestate', () =>
    HttpResponse.json(
      alpacaEnvelope([
        { Name: 'ShutterStatus', Value: 1 },
        { Name: 'Slewing', Value: false },
        { Name: 'TimeStamp', Value: new Date().toISOString() },
      ]),
    ),
  ),
  http.get('/management/v1/description', () =>
    HttpResponse.json(alpacaEnvelope({ ServerName: 'RoofRoof Demo', Manufacturer: 'RoofRoof', ManufacturerVersion: 'demo', Location: '' })),
  ),
  // REST — alerts
  http.get('/api/alerts/recent', () => HttpResponse.json({ enabled: true, armed: mockAlertsArmed.value, alerts: mockRecentAlerts })),
  http.get('/api/alerts/armed', () => HttpResponse.json(mockSchedule())),
  http.post('/api/alerts/arm', ({ request }) => {
    setMockArmed(request, true);
    return HttpResponse.json({ success: true, armed: true }, { status: 202 });
  }),
  http.post('/api/alerts/disarm', ({ request }) => {
    setMockArmed(request, false);
    return HttpResponse.json({ success: true, armed: false }, { status: 202 });
  }),
  http.post('/api/alerts/clear', () => {
    mockRecentAlerts.splice(0, mockRecentAlerts.length);
    return HttpResponse.json({ success: true });
  }),
  http.post('/api/alerts/test', () => HttpResponse.json({ success: true, message: 'Test notification queued' }, { status: 202 })),

  // REST — config
  http.get('/api/config', () => HttpResponse.json(mockConfig)),
  http.get('/api/roof/inputs', () => HttpResponse.json(mockInputs)),
  http.get('/api/roof/calibration', () => HttpResponse.json(mockCalibration)),
  http.post('/api/roof/calibration/start', () => HttpResponse.json({ success: true, state: 'running' })),
  http.post('/api/roof/calibration/abort', () => HttpResponse.json({ success: true, state: 'stopped' })),
  http.get('/api/settings/effective', () =>
    HttpResponse.json({ facts: mockFacts, settings: evaluate(mockConfig, mockFacts).map(({ blockedBy: _blockedBy, ...entry }) => entry) }),
  ),
  http.post('/api/config', () => HttpResponse.json({ success: true })),
  http.put('/api/config', () => HttpResponse.json({ success: true })),

  // REST — settings backup and restore (SQMeter spec 033): a file without secrets, a preview that changes the MQTT broker
  http.post('/api/backup/preview', () =>
    HttpResponse.json({
      token: '00112233445566778899aabbccddeeff',
      backup: { format: 1, firmware: '0.1.0', board: 'esp32', build: 'standard', deviceName: 'backyard-roof' },
      settings: { ...mockConfig, mqtt: { ...mockConfig.mqtt, broker: 'broker.lan' } },
      secrets: { state: 'none', changed: [], unchanged: [] },
      notApplicable: [],
      restart: [],
    }),
  ),
  http.post('/api/backup/apply', () => HttpResponse.json({ success: true, restarting: false })),
  http.post('/api/backup', () =>
    HttpResponse.json({
      roofroofBackup: 1,
      firmware: '0.1.0',
      board: 'esp32',
      build: 'standard',
      deviceName: mockConfig.deviceName,
      settings: mockConfig,
    }),
  ),

  // REST — wifi (returns { networks: [...] } to match firmware API shape)
  http.get('/api/wifi/scan', () => HttpResponse.json({ networks: mockWifiNetworks })),
  http.post('/api/wifi/connect', () => HttpResponse.json({ success: true, pending: true, message: 'Connection started' }, { status: 202 })),

  // REST — MQTT test
  http.post('/api/mqtt/test', () => HttpResponse.json({ success: true, message: 'Connection successful (demo)' })),

  // REST — control
  http.post('/api/restart', () => HttpResponse.json({ ok: true })),
  http.post('/api/update', () => HttpResponse.json({ success: true })),
  http.post('/api/update/fs', () => HttpResponse.json({ success: true })),

  // WebSocket — push system status every 2 seconds, like the device
  statusSocket.addEventListener('connection', ({ client }) => {
    const send = () =>
      client.send(
        JSON.stringify({
          ...mockStatus,
          uptime: demoUptime(),
          time: { iso: new Date().toISOString(), timezone: 'GMT0' },
        }),
      );

    send();
    const interval = setInterval(send, 2000);
    client.addEventListener('close', () => clearInterval(interval));
  }),
];
