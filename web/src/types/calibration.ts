// GET /api/roof/calibration (spec 016, specs/016-calibration-run/contracts/calibration-api.md).

export type CalibrationState = 'idle' | 'running' | 'done' | 'failed' | 'stopped';
export type CalibrationWhere = 'closed' | 'open' | 'partWay';
export type CalibrationLegName = 'toClosed' | 'slowOpen' | 'slowClose' | 'topOpen' | 'topClose';
export type HallLevelName = 'unsettled' | 'low' | 'between' | 'high' | 'rail';

export interface CalibrationProblem {
  reason: string;
  text: string;
}

export interface CalibrationPair<T> {
  old: T;
  new: T;
}

export interface CalibrationLeg {
  name: CalibrationLegName;
  target: 'open' | 'closed';
  dutyPercent: number;
  drivenMs: number;
  level: HallLevelName;
  speedMmPerSecond: number;
  ampsMean: number | null;
  ampsPeak: number | null;
  supplyMinVolts: number | null;
}

export interface CalibrationRest {
  mean: number;
  min: number;
  max: number;
  level: HallLevelName;
}

export interface CalibrationProposal {
  travelMm: CalibrationPair<number>;
  speedMmPerSecond: CalibrationPair<number> | null;
  timeoutMarginSeconds: CalibrationPair<number> | null;
  hallHighEnd: CalibrationPair<'open' | 'closed'> | null;
  hallHighCounts: CalibrationPair<number> | null;
  hallLowCounts: CalibrationPair<number> | null;
  speedProblem: CalibrationProblem | null;
  hallProblem: CalibrationProblem | null;
  warnings: CalibrationProblem[];
}

export interface CalibrationStatus {
  state: CalibrationState;
  active: boolean;
  request: { at: CalibrationWhere; travelMm: number; topPass: boolean; limitSeconds: number } | null;
  leg: {
    index: number;
    count: number;
    name: CalibrationLegName;
    phase: 'settle' | 'wait' | 'drive' | 'confirm';
    elapsedMs: number;
    limitMs: number;
  } | null;
  ending: CalibrationProblem | null;
  legs: CalibrationLeg[];
  hall: {
    between: { mean: number; min: number; max: number; noise: number } | null;
    closed: CalibrationRest | null;
    open: CalibrationRest | null;
  };
  currentSense: boolean;
  maxCurrentAmps: number;
  proposal: CalibrationProposal | null;
}

export interface CalibrationRequest {
  attended: boolean;
  at: CalibrationWhere;
  travelMm: number;
  topPass: boolean;
}
