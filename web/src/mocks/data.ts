import type {
  SystemStatus,
  AlpacaConfiguredDevice,
  AlertRecord,
  CalibrationStatus,
  RoofInputs,
  SqmeterSite,
  TeachStatus,
  WiFiNetwork,
} from '../types';

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
    closePending: false,
    calibrating: false,
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
    testBox: {
      set: false,
      active: false,
      park: false,
      weather: false,
      daylight: false,
      until: 0,
      expired: false,
      waitingForClock: false,
      parkRequests: true,
      text: '',
    },
  },
  // Spec 020: the location is typed in (and not yet set).
  location: { source: 'manual', set: false, gps: false, savedAt: null, readAgeSeconds: null, problem: null },
  // Spec 020: no group follows SQMeter.
  sync: {
    problem: null,
    readAgeSeconds: null,
    time: { follow: false, syncedAt: null, refused: null, needs: [] },
    mqtt: { follow: false, syncedAt: null, refused: null, needs: [] },
    alerts: { follow: false, syncedAt: null, refused: null, needs: [] },
  },
  // Spec 023: a DevKit (no Ethernet) with its preset in effect.
  ethernet: {
    mode: 'wifi',
    present: false,
    started: false,
    phyFound: false,
    link: false,
    speedMbps: 0,
    fullDuplex: false,
    ip: '',
    mac: '',
    ipv6: [],
  },
  pins: { preset: 'devkit', board: 'esp32-devkit', valid: true, restartNeeded: false },
};

// What POST /api/sqmeter/site answers (spec 020): SQMeter's site settings, its secrets only as "set".
export const mockSqmeterSite: SqmeterSite = {
  address: 'http://sqmeter.local',
  device: { name: 'SQMeter' },
  location: { set: true, latitude: 51.4769, longitude: -0.0005 },
  time: { timezone: 'GMT0BST,M3.5.0/1,M10.5.0', server1: 'pool.ntp.org', server2: 'time.nist.gov' },
  mqtt: { broker: '192.168.1.10', port: 1883, username: 'observatory', topic: 'sqmeter', passwordSet: true },
  ntfy: { server: 'https://ntfy.sh', topic: 'my-observatory', tokenSet: false },
  pushover: { userKeySet: true, appTokenSet: true },
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

// GET /api/roof/inputs (spec 015): SQMeter over HTTP good and safe, N.I.N.A. answering with the mount parked.
export const mockInputs: RoofInputs = {
  weather: {
    verdict: 'safe',
    text: 'safe',
    http: { enabled: true, state: 'good', generation: 1, ageMs: 2100, limitMs: 30000, safe: true, flags: 0, text: '' },
    mqtt: { enabled: false, state: 'off', generation: 0, ageMs: 0, limitMs: 150000, safe: false, flags: 0, text: '' },
  },
  park: { parked: true, text: 'parked' },
  nina: { state: 'good', generation: 1, connected: true, atPark: true, slewing: false, ageMs: 900, text: '' },
};

// GET /api/roof/calibration (spec 016): a finished run that proposes a faster speed.
export const mockCalibration: CalibrationStatus = {
  state: 'done',
  active: false,
  request: { at: 'closed', travelMm: 1850, topPass: false, limitSeconds: 180 },
  leg: null,
  ending: { reason: 'done', text: 'Calibration finished' },
  legs: [
    {
      name: 'slowOpen',
      target: 'open',
      dutyPercent: 40,
      drivenMs: 112800,
      level: 'high',
      speedMmPerSecond: 41.0,
      ampsMean: null,
      ampsPeak: null,
      supplyMinVolts: 12.3,
    },
    {
      name: 'slowClose',
      target: 'closed',
      dutyPercent: 40,
      drivenMs: 113100,
      level: 'low',
      speedMmPerSecond: 40.9,
      ampsMean: null,
      ampsPeak: null,
      supplyMinVolts: 12.3,
    },
  ],
  hall: {
    between: { mean: 2050, min: 2041, max: 2060, noise: 4.2 },
    closed: { mean: 702, min: 699, max: 705, level: 'low' },
    open: { mean: 3398, min: 3395, max: 3401, level: 'high' },
  },
  currentSense: false,
  maxCurrentAmps: 10,
  proposal: {
    travelMm: { old: 1850, new: 1850 },
    speedMmPerSecond: { old: 38, new: 41 },
    timeoutMarginSeconds: { old: 15, new: 10 },
    hallHighEnd: { old: 'open', new: 'open' },
    hallHighCounts: { old: 3000, new: 3000 },
    hallLowCounts: { old: 1100, new: 1100 },
    speedProblem: null,
    hallProblem: null,
    warnings: [],
  },
};

// GET /api/roof/teach (spec 019): the closed end and part-way marked, at rest.
export const mockTeach: TeachStatus = {
  jog: { active: false, id: 2, direction: null, elapsedMs: 0, limitMs: 30000, holdMs: 500, keepAliveMs: 150 },
  rest: { ready: true, counts: 2050 },
  marks: {
    closed: { count: 50, mean: 712.4, min: 709, max: 716, noise: 1.6 },
    open: null,
    partWay: { count: 50, mean: 2050.1, min: 2047, max: 2053, noise: 1.2 },
  },
  proposal: null,
};
