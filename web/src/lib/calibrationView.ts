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

export type ProposalKey = 'travelMm' | 'speedMmPerSecond' | 'timeoutMarginSeconds' | 'hallHighEnd' | 'hallHighCounts' | 'hallLowCounts';
export const PROPOSAL_KEYS: ProposalKey[] = [
  'travelMm',
  'speedMmPerSecond',
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

const shown = (key: ProposalKey, value: number | string): string => {
  if (key === 'hallHighEnd') return value === 'open' ? t('settings.roof.endOpen') : t('settings.roof.endClosed');
  return String(value);
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

/** The roof settings to save on Accept: every proposed value, nothing withheld. */
export const acceptedRoof = (proposal: CalibrationProposal, roof: RoofConfig): RoofConfig => {
  const next: RoofConfig = { ...roof };
  for (const key of PROPOSAL_KEYS) {
    const pair = proposal[key];
    if (pair) (next as unknown as Record<string, unknown>)[key] = pair.new;
  }
  return next;
};

/** Whether the run's saved values differ from the settings now (saved by someone else meanwhile). */
export const settingsChangedSince = (proposal: CalibrationProposal, roof: RoofConfig): boolean =>
  PROPOSAL_KEYS.some((key) => {
    const pair = proposal[key];
    return pair !== null && pair.old !== roof[key];
  });
