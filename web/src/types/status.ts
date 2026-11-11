import type { AlertSchedule, AlpacaClientState } from './alerts';

// GET /api/status and /ws/status (contract: specs/001-platform-bootstrap/contracts/schemas/status.schema.json).
export interface SystemStatus {
  firmware?: {
    name: string;
    product: 'roofroof';
    version: string;
    buildDate: string;
    buildTime: string;
    // 'other' when the partition table isn't RoofRoof's.
    layout?: 'l2' | 'other';
  };
  uptime: number;
  configRevision?: number; // goes up whenever the device saves its settings
  bootCount?: number;
  resetReason?: number; // ESP-IDF esp_reset_reason_t
  // Who holds the device's connections (SQMeter spec 011 FR-008).
  connections?: {
    tcpLimit: number;
    liveUpdates: { status: number; limitPerEndpoint: number; replaced: number; stalledClosed: number };
    alpacaRestoredAfterRestart: boolean;
  };
  freeHeap: number;
  minFreeHeap?: number;
  maxAllocHeap?: number;
  heapSize: number;
  stackFree?: { asyncTcp: number; loop: number; roof: number };
  heapStages?: { stage: string; free: number; largest: number }[];
  cpuFreqMHz: number;
  flashSize: number;
  sketchSize: number;
  freeSketchSpace: number;
  fsTotal: number;
  fsUsed: number;
  partitions?: {
    runningSlot: string;
    runningAddress: number;
    runningSize: number;
    bootSlot: string;
    nextSlot: string;
    nextSize: number;
    nvs?: {
      usedEntries: number;
      freeEntries: number;
      totalEntries: number;
      namespaceCount: number;
    };
    fsAddress: number;
    fsSize: number;
  };
  time: {
    /** Unix seconds; 0 until the device's clock is set. */
    epoch: number;
    /** Local time with its offset, for display. */
    iso: string;
    timezone: string;
  };
  ntp?: {
    enabled: boolean;
    synced: boolean;
    status: number;
    lastSync: number;
    nextSync: number;
    drift: number;
    server: string;
  };
  wifi: {
    connected: boolean;
    ssid: string;
    ip: string;
    rssi: number;
    mac: string;
    connectPending?: boolean;
    apMode?: boolean; // the "RoofRoof-Setup" hotspot is up
    hostname?: string;
    mdns?: boolean;
    // SQMeter spec 015: the setting the device is running with and its current addresses
    ipv6?: { enabled: boolean; addresses: { address: string; scope: 'link-local' | 'unique-local' | 'global' }[] };
  };
  mqtt?: {
    enabled: boolean;
    connected: boolean;
    state: number;
    lastPublish: number;
    lastReconnectAttempt: number;
    broker: string;
    port: number;
    topic: string;
    availabilityTopic?: string;
    clientId?: string;
  };
  alerts?: AlertSchedule;
  // The imaging app on each Alpaca device, keyed by device type: "dome", the
  // roof (spec 006); none in the demo.
  alpaca?: {
    enabled: boolean;
    clients: Record<string, AlpacaClientState>;
  };
  // The roof as its control task last saw it (spec 003, docs/api/rest.md).
  roof?: RoofStatus;
}

export type RoofState = 'unknown' | 'closed' | 'open' | 'opening' | 'closing' | 'stopped' | 'error';
export type RoofSimulation = 'off' | 'bench' | 'demo';
export type RoofSwitch = 'closed' | 'open' | 'unreadable' | 'notFitted';
// The automatic close on unsafe weather (spec 005's CloseSeq::Phase).
export type RoofClosePhase = 'idle' | 'waitPark' | 'closing' | 'retryWait' | 'heldByPerson' | 'cannotMove' | 'halted';

export interface RoofFault {
  kind: string; // Roof::faultName
  cause: 'logic' | 'watchdog' | 'parkInterlock' | 'storedUnreadable' | 'watchdogStopped' | 'benchSupply';
  measured: number | null;
  limit: number | null;
  text: string; // with the measured value and the limit
}

export interface RoofStatus {
  state: RoofState;
  positionKnown: boolean;
  positionMm: number | null;
  positionPercent: number | null;
  target: 'open' | 'closed' | null;
  driving: boolean;
  fromUnknown: boolean;
  moveElapsedMs: number;
  moveTimeoutMs: number;
  hall: { level: 'unsettled' | 'low' | 'between' | 'high' | 'rail'; counts: number };
  supplyVolts: number | null;
  supplyLow: boolean;
  minSupplyVolts: number;
  currentSense: boolean;
  motorAmps: number | null;
  maxCurrentAmps: number;
  parkSwitches: { fitted: number; switch1: RoofSwitch; switch2: RoofSwitch };
  rainContact: 'notFitted' | 'dry' | 'raining' | 'unreadable';
  closePending: boolean; // a close accepted, waiting for an upload or MQTT connect to stop
  // Spec 008: the automatic close's phase, and the weather and park as the roof's command checks see them now.
  closePhase: RoofClosePhase;
  weather: 'safe' | 'unsafe' | 'unknown';
  weatherText: string;
  parkConfirmed: boolean;
  parkText: string;
  fault: RoofFault | null;
  watchdogTrips: number;
  maxStepGapMs: number;
  maxWatchdogBeatAgeMs: number; // the oldest watchdog heartbeat a step saw since start (EN needs it <= 50 ms)
  // Spec 011: the real roof, bench firmware (simulated roof, motor never driven) or the browser demo.
  simulation: RoofSimulation;
}
