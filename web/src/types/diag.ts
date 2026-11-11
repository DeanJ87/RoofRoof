// Advanced diagnostics (spec 017, contracts/diagnostics.md): the live document
// (GET /api/diag, /ws/diag), the move summaries and one move's profile.
// Values the device can't read are null.

export type DiagPhase = 'idle' | 'deadTime' | 'slow' | 'softStart' | 'cruise' | 'slowing' | 'creep';
export type DiagDirection = 'off' | 'forward' | 'reverse';
export type DiagOrigin = 'person' | 'automatic' | 'client' | 'mqtt';
export type DiagResult = 'arrived' | 'stopped' | 'fault';

export interface DiagOff {
  enabled: false;
}

export interface DiagRoof {
  state: string;
  target: string | null;
  phase: DiagPhase;
  driving: boolean;
  direction: DiagDirection;
  duty: number;
  dutyPercent: number;
  en: boolean;
  rpwm: number;
  lpwm: number;
  supplyVolts: number | null;
  restVolts: number | null;
  sagVolts: number | null;
  currentSense: boolean;
  motorAmps: number | null;
  powerWatts: number | null;
  hallCounts: number;
  hall: string;
  positionKnown: boolean;
  positionMm: number | null;
  elapsedMs: number | null;
  remainingMs: number | null;
  timeoutMs: number | null;
  driveBudgetUsedMs: number | null;
  driveBudgetMs: number | null;
}

export interface DiagLive {
  enabled: true;
  log: 'ready' | 'unavailable';
  simulation: 'off' | 'bench' | 'demo';
  roof: DiagRoof;
  control: {
    periodMs: number;
    stepUs: number | null;
    maxStepUs: number | null;
    maxGapMs: number;
    watchdogLimitMs: number;
    watchdogMarginMs: number;
    maxBeatAgeMs: number;
    watchdogTrips: number;
  };
  loop: { passMs: number | null; maxPassMs: number | null };
  memory: {
    freeHeap: number | null;
    minFreeHeap: number | null;
    largestBlock: number | null;
    stackFree: { control: number | null; loop: number | null; network: number | null };
  };
  wifi: { rssi: number | null };
  uptimeS: number;
  resetReason: string | null;
  move: { id: number; active: boolean } | null;
}

export type DiagDocument = DiagLive | DiagOff;

export interface DiagFault {
  kind: string;
  measured: number | null;
  limit: number | null;
}

export interface DiagSummary {
  id: number;
  active: boolean;
  kept: boolean;
  profile: boolean;
  startMs: number | null;
  endMs: number | null;
  startEpoch: number;
  endEpoch: number;
  origin: DiagOrigin;
  target: string;
  endState: string;
  result: DiagResult;
  fault: DiagFault | null;
  durationMs: number;
  drivenMs: number;
  fromUnknown: boolean;
  /** A leg of the calibration run (spec 016). */
  calibration: boolean;
  peakAmps: number | null;
  averageAmps: number | null;
  stallAmps: number | null;
  restVolts: number | null;
  minVolts: number | null;
  sagVolts: number | null;
  hallStart: number | null;
  hallEnd: number | null;
}

export interface DiagMoves {
  enabled: true;
  keep: boolean;
  maxMoves: number;
  maxSummaries: number;
  moves: DiagSummary[];
}

// One profile point: [tMs, ampsAvg, ampsMax, voltsMin, dutyPercent].
export type DiagPoint = [number, number | null, number | null, number | null, number | null];

export interface DiagMove {
  enabled: true;
  move: DiagSummary;
  intervalMs: number;
  columns: string[];
  points: DiagPoint[];
}
