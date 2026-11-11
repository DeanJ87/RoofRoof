import type { SystemStatus, AlpacaConfiguredDevice, AlertRecord, WiFiNetwork } from '../types';

export { mockConfig } from './config';

export const mockStatus: SystemStatus = {
  alerts: { armed: true, mode: 'any', reason: 'none', since: null, sinceAgeMs: null },
  alpaca: {
    enabled: true,
    // The roof's Dome (spec 006), no imaging app yet.
    clients: { dome: { connected: false, watching: false, silent: false, lastCheckedAgeMs: null, clientId: null } },
  },
  firmware: {
    name: 'RoofRoof',
    product: 'roofroof',
    version: '0.1.0',
    buildDate: 'Nov 10 2026',
    buildTime: '12:00:00',
    layout: 'l2',
  },
  uptime: 7200,
  bootCount: 3,
  resetReason: 3,
  connections: {
    tcpLimit: 16,
    liveUpdates: { status: 1, limitPerEndpoint: 3, replaced: 0, stalledClosed: 0 },
    alpacaRestoredAfterRestart: false,
  },
  freeHeap: 214320,
  minFreeHeap: 156056,
  maxAllocHeap: 110580,
  heapSize: 327680,
  stackFree: { asyncTcp: 3360, loop: 3612, roof: 3480 },
  heapStages: [
    { stage: 'boot', free: 279836, largest: 110580 },
    { stage: 'wifi', free: 214276, largest: 110580 },
    { stage: 'setup complete', free: 190660, largest: 110580 },
  ],
  cpuFreqMHz: 240,
  flashSize: 4194304,
  // The l2 layout (SQMeter spec 027): two 1.75 MB app slots, 448 KB LittleFS.
  sketchSize: 1529280,
  freeSketchSpace: 305728,
  fsTotal: 458752,
  fsUsed: 184320,
  partitions: {
    runningSlot: 'app0',
    runningAddress: 0x10000,
    runningSize: 1835008,
    bootSlot: 'app0',
    nextSlot: 'app1',
    nextSize: 1835008,
    nvs: {
      usedEntries: 12,
      freeEntries: 488,
      totalEntries: 500,
      namespaceCount: 1,
    },
    fsAddress: 0x390000,
    fsSize: 458752,
  },
  time: {
    epoch: Math.floor(Date.now() / 1000),
    iso: new Date().toISOString(),
    timezone: 'GMT0',
  },
  ntp: {
    enabled: true,
    synced: true,
    status: 1,
    lastSync: Date.now() - 3600000,
    nextSync: Date.now() + 3600000,
    drift: 0.003,
    server: 'pool.ntp.org',
  },
  wifi: {
    connected: true,
    ssid: 'DarkSkyLab',
    ip: '192.168.1.42',
    rssi: -58,
    mac: 'AA:BB:CC:DD:EE:FF',
    connectPending: false,
    apMode: false,
    hostname: 'roofroof',
    mdns: true,
    ipv6: {
      enabled: true,
      addresses: [
        { address: 'fe80::a3b2:c3ff:fed4:e5f6', scope: 'link-local' },
        { address: '2001:db8:4a2c:1:a3b2:c3ff:fed4:e5f6', scope: 'global' },
      ],
    },
  },
  mqtt: {
    enabled: true,
    connected: true,
    state: 0,
    lastPublish: Date.now() - 60000,
    lastReconnectAttempt: 0,
    broker: 'mqtt.example.com',
    port: 1883,
    topic: 'roofroof',
    availabilityTopic: 'roofroof/availability',
    clientId: 'RoofRoof-D4E5F6',
  },
  // The roof's status section for the unit tests: a closed roof (the demo's own simulated roof is spec 011's).
  roof: {
    state: 'closed',
    positionKnown: true,
    positionMm: 0,
    positionPercent: 0,
    target: null,
    driving: false,
    fromUnknown: false,
    moveElapsedMs: 0,
    moveTimeoutMs: 0,
    hall: { level: 'low', counts: 712 },
    supplyVolts: 12.46,
    supplyLow: false,
    minSupplyVolts: 11,
    currentSense: false,
    motorAmps: null,
    maxCurrentAmps: 10,
    parkSwitches: { fitted: 0, switch1: 'notFitted', switch2: 'notFitted' },
    rainContact: 'notFitted',
    closePending: false,
    closePhase: 'idle',
    weather: 'safe',
    weatherText: 'safe',
    parkConfirmed: true,
    parkText: 'parked (N.I.N.A. AtPark)',
    daylight: { mode: 'alert', state: 'night', sunAltitudeDeg: -32.5, limitDeg: 0, text: 'Sun -32.5° < 0.0°', closes: false },
    fault: null,
    watchdogTrips: 0,
    maxStepGapMs: 21,
    maxWatchdogBeatAgeMs: 12,
    simulation: 'off',
  },
};

export const mockWifiNetworks: WiFiNetwork[] = [
  { ssid: 'DarkSkyLab', rssi: -42, encryption: 'secured' },
  { ssid: 'NeighbourNet', rssi: -71, encryption: 'secured' },
  { ssid: 'Observatory-Guest', rssi: -78, encryption: 'open' },
  { ssid: 'TeleCom_5G', rssi: -85, encryption: 'secured' },
];

// The roof's Dome (spec 006), as /management/v1/configureddevices lists it.
export const mockAlpacaDevices: AlpacaConfiguredDevice[] = [
  { DeviceName: 'RoofRoof roof', DeviceType: 'Dome', DeviceNumber: 0, UniqueID: 'roofroof-a1b2c3d4e5f6-dome-0' },
];

export const mockRecentAlerts: AlertRecord[] = [
  {
    id: 2,
    event: 'client_back',
    title: 'Imaging app is back',
    message: 'RoofRoof Dome is being checked again.',
    level: 'normal',
    ageSeconds: 1200,
    channels: { pushover: { status: 'sent', detail: 'HTTP 200' } },
  },
  {
    id: 1,
    event: 'client_lost',
    title: 'Imaging app stopped checking',
    message: 'No request to RoofRoof Dome for 2 min (last 22:59).',
    level: 'urgent',
    ageSeconds: 1330,
    channels: { pushover: { status: 'sent', detail: 'HTTP 200' } },
  },
];
