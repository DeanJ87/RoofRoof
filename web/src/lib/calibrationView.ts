import type { CalibrationProposal, CalibrationWhere, RoofConfig, RoofStatus } from '../types';
import { t } from '../i18n';

// What the calibration page shows (spec 016), as pure functions for the tests.

/** Where the roof seems to be, as the start form's first guess (the person confirms it). */
export const whereFromState = (roof: RoofStatus | undefined): CalibrationWhere => {
  if (roof?.hall.level === 'low' || roof?.hall.level === 'high') return roof.state === 'open' ? 'open' : 'closed';
  return 'partWay';
};

/** Why Start can't be pressed now, or '' when it can: the first blocker (spec 026), then the confirmation. */
export const startBlocked = (blocked: string, attended: boolean): string => blocked || (attended ? '' : t('calibrate.reason.notAttended'));

export type ProposalKey =
  'travelMm' | 'speedMmPerSecond' | 'slowSpeedMmPerSecond' | 'timeoutMarginSeconds' | 'hallHighEnd' | 'hallHighCounts' | 'hallLowCounts';
export const PROPOSAL_KEYS: ProposalKey[] = [
  'travelMm',
  'speedMmPerSecond',
  'slowSpeedMmPerSecond',
  'timeoutMarginSeconds',
  'hallHighEnd',
  'hallHighCounts',
  'hallLowCounts',
];

export interface ProposalRow {
  key: ProposalKey;
  saved: string; // the value saved now
  proposed: string | null; // null: withheld
  changed: boolean;
}

// A measured speed arrives as a float ("38.79999924"): two decimals are plenty, here and when saved.
const tidy = (value: number) => Math.round(value * 100) / 100;

const shown = (key: ProposalKey, value: number | string): string => {
  if (key === 'hallHighEnd') return value === 'open' ? t('settings.roof.endOpen') : t('settings.roof.endClosed');
  return String(typeof value === 'number' ? tidy(value) : value);
};

/** Each proposed value beside the value saved now (not the one at the start of the run). */
export const proposalRows = (proposal: CalibrationProposal, roof: RoofConfig): ProposalRow[] =>
  PROPOSAL_KEYS.map((key) => {
    const pair = proposal[key];
    const saved = roof[key];
    return {
      key,
      saved: shown(key, saved),
      proposed: pair ? shown(key, pair.new) : null,
      changed: pair !== null && pair.new !== saved,
    };
  });

// Spec 026: the RoofConfig::MEASURED_* bit of each proposed value.
const MEASURED_BIT: Record<ProposalKey, number> = {
  travelMm: 1,
  speedMmPerSecond: 2,
  slowSpeedMmPerSecond: 2,
  timeoutMarginSeconds: 4,
  hallHighEnd: 8,
  hallHighCounts: 8,
  hallLowCounts: 8,
};
const DIRECTION_BIT = 16; // the run's open-end question checked the direction

/** The roof settings to save on Accept: every proposed value, nothing withheld, marked measured (spec 026). */
export const acceptedRoof = (proposal: CalibrationProposal, roof: RoofConfig): RoofConfig => {
  const next: RoofConfig = { ...roof };
  let measured = (roof.measured ?? 0) | DIRECTION_BIT;
  for (const key of PROPOSAL_KEYS) {
    const pair = proposal[key];
    if (!pair) continue;
    (next as unknown as Record<string, unknown>)[key] = typeof pair.new === 'number' ? tidy(pair.new) : pair.new;
    measured |= MEASURED_BIT[key];
  }
  // The slow speed belongs to the slow duty it was measured at (it counts only while that is the slow duty).
  if (proposal.slowSpeedDutyPercent) next.slowSpeedDutyPercent = proposal.slowSpeedDutyPercent.new;
  return roof.measured === undefined ? next : { ...next, measured };
};

/** Whether the run's saved values differ from the settings now (saved by someone else meanwhile). */
export const settingsChangedSince = (proposal: CalibrationProposal, roof: RoofConfig): boolean =>
  PROPOSAL_KEYS.some((key) => {
    const pair = proposal[key];
    return pair !== null && pair.old !== roof[key];
  });
