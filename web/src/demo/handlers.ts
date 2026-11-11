import { http, HttpResponse, ws } from 'msw';
import { DEMO_VERSION, demoDevice, type Reply } from './device';
import { backupReply } from './backup';
import { existingRealSender, withRealResults } from './realSend';
import { mockStatus, mockWifiNetworks } from '../mocks/data';

// The demo's requests, answered by the emulated device (./device.ts) the way
// the firmware answers them. Anything a real device would send somewhere
// (alerts, MQTT, WiFi, uploads) is simulated and marked `demo: true`;
// nothing leaves the browser (SQMeter spec 016, FR-006).

const json = (body: string, status = 200) => new HttpResponse(body, { status, headers: { 'Content-Type': 'application/json' } });

const reply = (r: Reply) =>
  new HttpResponse(r.body, { status: r.status, headers: { 'Content-Type': r.contentType ?? 'application/json' } });

// Who paused or resumed alerts: the web UI adds ?source=ui, scripts don't.
const armSource = (request: Request) => (new URL(request.url).searchParams.get('source') === 'ui' ? 'ui' : 'rest');

const queryParams = (request: Request) => Object.fromEntries(new URL(request.url).searchParams.entries());

// Alpaca parameters: query string for GET, form body for PUT.
async function alpacaParams(request: Request): Promise<[string, string][]> {
  const params = [...new URL(request.url).searchParams.entries()];
  if (request.method === 'PUT') {
    const text = await request.text();
    params.push(...new URLSearchParams(text).entries());
  }
  return params;
}

// GET /api/status: the device core's state (uptime, alerts, Alpaca
// clients) with the hardware sections a real ESP32 would report.
// A dual-stack home network: link-local plus a SLAAC global address
// (documentation prefix 2001:db8::/32, SQMeter spec 015). The setting applies at
// boot: the page load, or a restart.
let ipv6AtBoot: boolean | null = null;
const demoIpv6 = (cfg: { wifi?: { ipv6?: boolean } }) => {
  ipv6AtBoot ??= cfg.wifi?.ipv6 ?? true;
  return ipv6Addresses(ipv6AtBoot);
};
const ipv6Addresses = (running: boolean) => ({
  enabled: running,
  addresses: running
    ? [
        { address: 'fe80::a3b2:c3ff:fed4:e5f6', scope: 'link-local' as const },
        { address: '2001:db8:4a2c:1:a3b2:c3ff:fed4:e5f6', scope: 'global' as const },
      ]
    : [],
});

// Like the device's alerts.recentRevision: changes whenever the recent-alerts
// list would read differently (a new alert, a channel result, a clear), not
// with the ages that tick by every second.
const recentRevision = () => {
  const recent = JSON.parse(demoDevice.recentAlerts()) as { alerts?: { id: number; channels?: unknown }[] };
  const key = (recent.alerts ?? []).map((a) => `${a.id}:${JSON.stringify(a.channels ?? {})}`).join('|');
  let hash = 0;
  for (let i = 0; i < key.length; i++) hash = (hash * 31 + key.charCodeAt(i)) >>> 0;
  return hash;
};

// The roof's state and fault: the device pushes a change at once (spec 003).
const roofRevision = () => {
  const roof = demoDevice.statusParts().roof as { state?: string; fault?: { kind?: string } | null } | undefined;
  return `${roof?.state}:${roof?.fault?.kind ?? ''}`;
};

// Like the device's configRevision: changes when the settings change.
const configRevision = () => {
  const text = JSON.stringify(demoDevice.rawConfig());
  let hash = 0;
  for (let i = 0; i < text.length; i++) hash = (hash * 31 + text.charCodeAt(i)) >>> 0;
  return hash;
};

export function statusDocument() {
  const parts = demoDevice.statusParts();
  const cfg = demoDevice.rawConfig();
  return {
    ...mockStatus,
    ...parts,
    alerts: { ...parts.alerts, recentRevision: recentRevision() },
    configRevision: configRevision(),
    firmware: { ...mockStatus.firmware, version: DEMO_VERSION },
    time: { epoch: Math.floor(demoDevice.now.getTime() / 1000), iso: demoDevice.isoTime, timezone: cfg.ntp?.timezone ?? 'UTC0' },
    wifi: {
      ...mockStatus.wifi,
      ssid: joinedSsid ?? (cfg.wifi?.ssid || mockStatus.wifi.ssid),
      hostname: cfg.wifi?.hostname,
      mdns: cfg.wifi?.mdns ?? true,
      ipv6: demoIpv6(cfg),
      apMode: false,
      connectPending: false,
    },
    mqtt: cfg.mqtt?.enabled
      ? {
          ...mockStatus.mqtt,
          enabled: true,
          connected: true,
          broker: cfg.mqtt.broker,
          port: cfg.mqtt.port,
          topic: cfg.mqtt.topic,
          availabilityTopic: `${cfg.mqtt.topic}/availability`,
        }
      : { ...mockStatus.mqtt, enabled: false, connected: false, availabilityTopic: `${cfg.mqtt?.topic ?? 'roofroof'}/availability` },
  };
}

// The network the WiFi setup screen "joined" (simulated).
let joinedSsid: string | null = null;

// Spec 010, as the device answers it: no settings save, restore, upload or
// restart while the roof moves or the automatic close works (409 with the reason).
const held = (action = 'flashWrite') => {
  const why = demoDevice.platformHold(action);
  return why ? HttpResponse.json({ error: why }, { status: 409 }) : null;
};

const simulated = (extra: Record<string, unknown> = {}) => HttpResponse.json({ success: true, demo: true, ...extra });

export const demoHandlers = [
  // Status
  http.get('/api/status', () => HttpResponse.json(statusDocument())),
  http.get('/api/settings/effective', () => json(demoDevice.effective())),
  http.get('/api/roof/inputs', () => json(demoDevice.inputsDocument())),

  // Settings
  http.get('/api/config', () => json(demoDevice.getConfig())),
  http.post('/api/config', async ({ request }) => held() ?? reply(demoDevice.applyConfig(await request.text()))),
  http.put('/api/config', async ({ request }) => held() ?? reply(demoDevice.applyConfig(await request.text()))),
  // Settings backup and restore (SQMeter spec 033); a restore that needs it restarts the demo device.
  ...(['preview', 'apply', 'backup'] as const).map((route) =>
    http.post(route === 'backup' ? '/api/backup' : `/api/backup/${route}`, async ({ request }) => {
      const hold = route === 'apply' ? held() : null;
      if (hold) return hold;
      const answer = backupReply(demoDevice.backupCore(), route, await request.text());
      if (answer.restarting) demoDevice.restart();
      return reply(answer.reply);
    }),
  ),
  http.post('/api/restart', () => {
    const hold = held('restart');
    if (hold) return hold;
    demoDevice.restart();
    ipv6AtBoot = null;
    return HttpResponse.json({ success: true, message: 'Restarting...' });
  }),

  // Alerts
  http.get('/api/alerts/recent', () => json(withRealResults(demoDevice.recentAlerts()))),
  http.get('/api/alerts/armed', () => json(demoDevice.armedDocument())),
  http.post('/api/alerts/arm', ({ request }) => {
    demoDevice.setArmed(true, armSource(request));
    return HttpResponse.json({ success: true, armed: true }, { status: 202 });
  }),
  http.post('/api/alerts/disarm', ({ request }) => {
    demoDevice.setArmed(false, armSource(request));
    return HttpResponse.json({ success: true, armed: false }, { status: 202 });
  }),
  http.post('/api/alerts/clear', () => {
    demoDevice.clearAlerts();
    return HttpResponse.json({ success: true });
  }),
  http.post('/api/alerts/test', ({ request }) => {
    const result = demoDevice.testAlert(queryParams(request));
    existingRealSender()?.check(); // opted-in real notifications (SQMeter spec 018)
    return reply(result);
  }),

  // The roof (spec 011): the device's own command checks and motion logic, on the simulated roof
  ...(['open', 'close', 'stop', 'clear-fault'] as const).map((name) =>
    http.post(`/api/roof/${name}`, () => reply(demoDevice.roofCommand(name))),
  ),

  // Network (simulated: nothing is contacted)
  http.get('/api/wifi/scan', () => HttpResponse.json({ success: true, scanning: false, networks: mockWifiNetworks })),
  http.post('/api/wifi/connect', async ({ request }) => {
    const body = (await request.json().catch(() => ({}))) as { ssid?: string; password?: string };
    if (typeof body.ssid !== 'string' || typeof body.password !== 'string')
      return HttpResponse.json({ error: 'Missing SSID or password' }, { status: 400 });
    joinedSsid = body.ssid; // nothing is joined: the demo just says it was
    return HttpResponse.json({ success: true, pending: true, message: 'Connection started', demo: true }, { status: 202 });
  }),
  http.post('/api/mqtt/test', () => simulated({ message: 'Demo: nothing was sent - a real RoofRoof would connect to your broker here' })),

  // Updates (simulated: nothing flashed)
  http.post('/api/update', () => held('firmwareUpload') ?? simulated()),
  http.post('/api/update/fs', () => held('filesystemUpload') ?? simulated()),

  // ASCOM Alpaca: the firmware's own router, in the device core
  http.get('/management/*', async ({ request }) =>
    reply(demoDevice.alpaca('GET', new URL(request.url).pathname, await alpacaParams(request))),
  ),
  http.get('/api/v1/*', async ({ request }) => reply(demoDevice.alpaca('GET', new URL(request.url).pathname, await alpacaParams(request)))),
  http.put('/api/v1/*', async ({ request }) => reply(demoDevice.alpaca('PUT', new URL(request.url).pathname, await alpacaParams(request)))),

  // Live updates, at the device's rates
  ws.link('*/ws/status').addEventListener('connection', ({ client }) => {
    const send = () => client.send(JSON.stringify(statusDocument()));
    send();
    const interval = setInterval(send, 2000);
    // As on the device: a change to the recent alerts, the settings or the
    // roof's state or fault is pushed at once.
    const revisions = () => `${recentRevision()}:${configRevision()}:${roofRevision()}`;
    let lastRevision = revisions();
    const unsubscribe = demoDevice.onChange(() => {
      const revision = revisions();
      if (revision !== lastRevision) {
        lastRevision = revision;
        send();
      }
    });
    client.addEventListener('close', () => {
      clearInterval(interval);
      unsubscribe();
    });
  }),
];
