import { http, HttpResponse, ws } from 'msw';
import type { AlertScheduleReason } from '../types';
import {
  mockStatus,
  mockConfig,
  mockInputs,
  mockWifiNetworks,
  mockAlpacaDevices,
  mockRecentAlerts,
  mockCalibration,
  mockTeach,
  mockSqmeterSite,
} from './data';
import { evaluate, type DepFacts } from '../lib/settingsDeps';
import { mockCrashesText, mockDeviceLogText } from './crash';
import { mockDiagLive, mockDiagMove, mockDiagMoves } from './diag';
import { mockValidate, mockWiring, mockWiringDone, pinsDocumentFor } from './pins';

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

// Advanced diagnostics (spec 017): off, as on a new device; tests switch it on.
export const mockDiag = { enabled: false };
const diagOff = () => HttpResponse.json({ enabled: false });
const diagSocket = ws.link('*/ws/diag');

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
  // Spec 027: Open and Close as the device answers a still roof; Stop always.
  http.post('/api/roof/open', () => HttpResponse.json({ success: true, state: 'opening' })),
  http.post('/api/roof/close', () => HttpResponse.json({ success: true, state: 'closed' })),
  http.post('/api/roof/stop', () => HttpResponse.json({ success: true, state: 'stopped' })),
  // Spec 027: Settings' N.I.N.A. Test: answering, the mount connected and parked.
  http.post('/api/nina/test', () => HttpResponse.json({ reachable: true, connected: true, atPark: true, slewing: false })),
  http.get('/api/roof/calibration', () => HttpResponse.json(mockCalibration)),
  http.post('/api/roof/calibration/start', () => HttpResponse.json({ success: true, state: 'running' })),
  http.post('/api/roof/calibration/abort', () => HttpResponse.json({ success: true, state: 'stopped' })),
  http.post('/api/roof/calibration/confirm', () => HttpResponse.json({ success: true, state: 'open' })),
  http.get('/api/roof/teach', () => HttpResponse.json(mockTeach)),
  http.post('/api/roof/jog', () => HttpResponse.json({ success: true, state: 'opening', id: 1, holdMs: 500, keepAliveMs: 150 })),
  http.post('/api/roof/teach/mark', () => HttpResponse.json({ success: true, state: 'closed' })),
  http.post('/api/roof/teach/confirm', () => HttpResponse.json({ success: true })),
  http.post('/api/roof/teach/discard', () => HttpResponse.json({ success: true })),
  http.get('/api/settings/effective', () =>
    HttpResponse.json({ facts: mockFacts, settings: evaluate(mockConfig, mockFacts).map(({ blockedBy: _blockedBy, ...entry }) => entry) }),
  ),
  http.post('/api/config', () => HttpResponse.json({ success: true })),
  // Spec 020: SQMeter's site settings for the copy (a typed address answers the same).
  http.post('/api/sqmeter/site', () => HttpResponse.json(mockSqmeterSite)),
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

  // REST — advanced diagnostics (spec 017)
  http.get('/api/diag', () => (mockDiag.enabled ? HttpResponse.json(mockDiagLive) : diagOff())),
  http.get('/api/diag/moves', () => (mockDiag.enabled ? HttpResponse.json(mockDiagMoves) : diagOff())),
  http.get('/api/diag/moves/:id', ({ params, request }) => {
    if (!mockDiag.enabled || Number(params.id) !== mockDiagMove.move.id)
      return HttpResponse.json({ error: "That move isn't held any more" }, { status: 404 });
    if (new URL(request.url).searchParams.get('format') !== 'csv') return HttpResponse.json(mockDiagMove);
    const rows = mockDiagMove.points.map((point) => point.map((value) => value ?? '').join(','));
    return new HttpResponse(['t_ms,amps_avg,amps_max,volts_min,duty_percent', ...rows, ''].join('\r\n'), {
      headers: { 'Content-Type': 'text/csv', 'Content-Disposition': `attachment; filename="roofroof-move-${params.id}.csv"` },
    });
  }),
  http.post('/api/diag/moves/clear', () => (mockDiag.enabled ? HttpResponse.json({ success: true }) : diagOff())),
  // Crash capture (spec 029): always on, whatever the diagnostics mode
  http.get('/api/diag/crashes', () => HttpResponse.text(mockCrashesText)),
  http.get('/api/diag/log', () => HttpResponse.text(mockDeviceLogText)),

  // REST — the board and its pins (spec 023): a DevKit with its preset; the check finishes at once
  http.get('/api/pins', () => HttpResponse.json(pinsDocumentFor('devkit'))),
  http.post('/api/pins/validate', async ({ request }) => {
    const body: unknown = await request.json().catch(() => null);
    if (!body || typeof body !== 'object' || Array.isArray(body))
      return HttpResponse.json({ error: 'A pin map must be a JSON object' }, { status: 400 });
    return HttpResponse.json(mockValidate(body));
  }),
  http.post('/api/pins/check', () => {
    mockWiring.state = mockWiringDone;
    return HttpResponse.json({ started: true }, { status: 202 });
  }),
  http.get('/api/pins/check', () => HttpResponse.json(mockWiring.state)),

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
  // Closed at once while off, like the device.
  diagSocket.addEventListener('connection', ({ client }) => {
    if (!mockDiag.enabled) {
      client.close();
      return;
    }
    client.send(JSON.stringify(mockDiagLive));
    const interval = setInterval(() => client.send(JSON.stringify(mockDiagLive)), 2000);
    client.addEventListener('close', () => clearInterval(interval));
  }),
];
