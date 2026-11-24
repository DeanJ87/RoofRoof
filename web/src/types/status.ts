import type { AlertSchedule, AlpacaClientState } from './alerts';
import type { EthernetStatus, PinsStatus } from './pins';

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
  // Where the daylight check's location came from (spec 020). Absent on older firmware.
  location?: LocationStatus;
  // Each group's sync with SQMeter's settings (spec 020). Absent on older firmware.
  sync?: SyncStatus;
  // Spec 023: Ethernet on a board that has it, and the board and pins in effect. Absent on older firmware.
  ethernet?: EthernetStatus;
  pins?: PinsStatus;
}

export type SyncProblem = 'noAddress' | 'waiting' | 'unreachable' | 'unreadable' | 'passwordNeeded' | 'passwordRefused';
export type SyncNeed = 'mqttPassword' | 'ntfyToken' | 'pushoverKeys';
export interface SyncGroupStatus {
  follow: boolean;
  syncedAt: number | null; // Unix s of the last sync
  refused: string | null; // why SQMeter's value wasn't taken (the device's words)
  needs: SyncNeed[]; // secrets SQMeter has and RoofRoof lacks
}
// Spec 024: why SQMeter's passwords couldn't be read (the copy's answer and the sync status).
export type SecretsProblem =
  | 'waiting'
  | 'notSaved'
  | 'notSupported'
  | 'sharingOff'
  | 'noWebPassword'
  | 'loginNeeded'
  | 'loginRefused'
  | 'tooManyLogins'
  | 'unreachable'
  | 'unreadable';
export interface SyncStatus {
  problem: SyncProblem | null;
  // Spec 024: the last read of SQMeter's passwords while MQTT or alerts follows (absent on older firmware).
  secrets?: 'received' | SecretsProblem | null;
  readAgeSeconds: number | null; // since the last good read of SQMeter's settings in this run
  time: SyncGroupStatus;
  mqtt: SyncGroupStatus;
  alerts: SyncGroupStatus;
}

export type LocationProblem = SyncProblem | 'noLocation';
export interface LocationStatus {
  source: 'sqmeter' | 'manual';
  set: boolean;
  gps: boolean; // the kept value is SQMeter's GPS fix
  savedAt: number | null; // Unix seconds of the last save from SQMeter
  readAgeSeconds: number | null; // the last good read in this run
  problem: LocationProblem | null; // the last read's problem
}

export type RoofState = 'unknown' | 'closed' | 'open' | 'opening' | 'closing' | 'stopped' | 'error';
export type RoofSimulation = 'off' | 'bench' | 'demo';
export type RoofSwitch = 'closed' | 'open' | 'unreadable' | 'notFitted';
// The automatic close on unsafe weather (spec 005's CloseSeq::Phase).
export type RoofClosePhase = 'idle' | 'waitPark' | 'closing' | 'retryWait' | 'heldByPerson' | 'cannotMove' | 'halted';

export interface RoofFault {
  kind: string; // Roof::faultName
  cause: 'logic' | 'watchdog' | 'parkInterlock' | 'storedUnreadable' | 'watchdogStopped' | 'benchSupply' | 'pinSetup'; // pinSetup: spec 023
  measured: number | null;
  limit: number | null;
  text: string; // with the measured value and the limit
}

// The daylight failsafe as the roof sees it now (spec 013). Absent on older firmware.
export interface RoofDaylight {
  mode: 'off' | 'alert' | 'close';
  state: 'off' | 'unknown' | 'night' | 'day';
  sunAltitudeDeg: number | null; // null while off or unknown
  limitDeg: number;
  text: string; // "Sun 3.2° ≥ 0.0°", "Location not set", "Clock not set", "Off"
  closes: boolean; // daylight wants the roof closed now (mode close and day)
}

// Spec 022: the test box's bypasses as the checks apply them now. Absent on older firmware.
export interface RoofTestBox {
  set: boolean; // any bypass set in the settings
  active: boolean; // any in effect now
  park: boolean; // each: set and in effect now
  weather: boolean;
  daylight: boolean;
  until: number; // the deadline, Unix seconds (0: none)
  expired: boolean;
  waitingForClock: boolean;
  parkRequests: boolean; // unsafe weather would ask a configured N.I.N.A. to park its mount
  text: string; // the device's text naming them, "" when none is set
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
  parkSwitches: {
    fitted: number;
    switch1: RoofSwitch;
    switch2: RoofSwitch;
    // Spec 024 (absent on older firmware): the switches alone confirm the park; each switch's roof
    // openings since it last read open; the stuck-switch check's refusal of Open (device text) or null.
    switchesOnly?: boolean;
    openings?: [number, number];
    check?: string | null;
  };
  closePending: boolean; // a close accepted, waiting for an upload or MQTT connect to stop
  calibrating?: boolean; // spec 016: a calibration run is under way (pauses included)
  // Spec 008: the automatic close's phase, and the weather and park as the roof's command checks see them now.
  closePhase: RoofClosePhase;
  // Spec 026: a stand-down's seconds left if nothing wants the roof closed meanwhile (null: not counting; absent: older firmware).
  closeRearmSeconds?: number | null;
  weather: 'safe' | 'unsafe' | 'unknown';
  weatherText: string;
  parkConfirmed: boolean;
  parkText: string;
  daylight?: RoofDaylight;
  fault: RoofFault | null;
  watchdogTrips: number;
  maxStepGapMs: number;
  maxWatchdogBeatAgeMs: number; // the oldest watchdog heartbeat a step saw since start (EN needs it <= 50 ms)
  // Spec 011: the real roof, bench firmware (simulated roof, motor never driven) or the browser demo.
  simulation: RoofSimulation;
  testBox?: RoofTestBox;
}
