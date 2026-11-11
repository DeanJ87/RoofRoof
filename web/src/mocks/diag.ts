import type { DiagLive, DiagMove, DiagMoves, DiagSummary } from '../types/diag';

// Advanced diagnostics (spec 017) as a device with it on would answer
// (contracts/diagnostics.md). The mock handlers answer `{"enabled":false}`
// unless a test switches it on.

export const mockDiagLive: DiagLive = {
  enabled: true,
  log: 'ready',
  simulation: 'off',
  roof: {
    state: 'opening',
    target: 'open',
    phase: 'cruise',
    driving: true,
    direction: 'forward',
    duty: 255,
    dutyPercent: 100,
    en: true,
    rpwm: 255,
    lpwm: 0,
    supplyVolts: 12.31,
    restVolts: 12.62,
    sagVolts: 0.31,
    currentSense: true,
    motorAmps: 3.42,
    powerWatts: 42.1,
    hallCounts: 2050,
    hall: 'between',
    positionKnown: true,
    positionMm: 812.5,
    elapsedMs: 21400,
    remainingMs: 27300,
    timeoutMs: 63700,
    driveBudgetUsedMs: 20900,
    driveBudgetMs: 177000,
  },
  control: {
    periodMs: 20,
    stepUs: 412,
    maxStepUs: 980,
    maxGapMs: 21,
    watchdogLimitMs: 100,
    watchdogMarginMs: 79,
    maxBeatAgeMs: 11,
    watchdogTrips: 0,
  },
  loop: { passMs: 3, maxPassMs: 41 },
  memory: { freeHeap: 101234, minFreeHeap: 84512, largestBlock: 65524, stackFree: { control: 3120, loop: 6044, network: 4380 } },
  wifi: { rssi: -61 },
  uptimeS: 12345,
  resetReason: 'power-on',
  move: { id: 12, active: true },
};

export const mockDiagSummary: DiagSummary = {
  id: 11,
  active: false,
  kept: false,
  profile: true,
  startMs: 1203400,
  endMs: 1253900,
  startEpoch: 1762898400,
  endEpoch: 1762898450,
  origin: 'person',
  target: 'open',
  endState: 'open',
  result: 'arrived',
  fault: null,
  durationMs: 50500,
  drivenMs: 49900,
  fromUnknown: false,
  peakAmps: 6.1,
  averageAmps: 3.3,
  stallAmps: null,
  restVolts: 12.62,
  minVolts: 12.01,
  sagVolts: 0.61,
  hallStart: 1020,
  hallEnd: 3120,
};

// A move from before the last restart: no profile, no uptime times.
export const mockDiagKept: DiagSummary = {
  ...mockDiagSummary,
  id: 4,
  kept: true,
  profile: false,
  startMs: null,
  endMs: null,
  origin: 'automatic',
  target: 'closed',
  endState: 'open',
  result: 'fault',
  fault: { kind: 'overCurrent', measured: 9.2, limit: 8 },
};

export const mockDiagMoves: DiagMoves = {
  enabled: true,
  keep: true,
  maxMoves: 5,
  maxSummaries: 32,
  moves: [mockDiagSummary, mockDiagKept],
};

export const mockDiagMove: DiagMove = {
  enabled: true,
  move: mockDiagSummary,
  intervalMs: 500,
  columns: ['tMs', 'ampsAvg', 'ampsMax', 'voltsMin', 'dutyPercent'],
  points: [
    [0, 1.2, 4.8, 12.55, 40],
    [500, 2.95, 3.1, 12.2, 62],
    [1000, 3.3, 3.6, 12.05, 100],
    [1500, 3.2, 3.4, 12.01, 100],
    [2000, null, null, null, 0],
  ],
};
