import type { CalibrationProposal, CalibrationWhere, RoofConfig, RoofStatus } from '../types';
import { t } from '../i18n';
import { deviceText } from '../i18n/deviceMessage';

// What the calibration page shows (spec 016), as pure functions for the tests.

export interface CalibrationCheck {
  id: 'weather' | 'park' | 'fault' | 'sequence' | 'still' | 'supply' | 'hall';
  label: string;
  ok: boolean;
  text: string;
}

/** What a run needs, from the live status (the device checks again when it starts). */
export const calibrationChecks = (roof: RoofStatus): CalibrationCheck[] => {
  const daylight = roof.daylight?.closes === true;
  const weatherOk = roof.weather === 'safe' && !daylight;
  const moving = roof.state === 'opening' || roof.state === 'closing' || roof.closePending;
  const hallOk = roof.hall.level !== 'rail' && roof.hall.level !== 'unsettled';
  return [
    {
      id: 'weather',
      label: t('calibrate.check.weather'),
      ok: weatherOk,
      text: daylight ? deviceText(roof.daylight?.text) : deviceText(roof.weatherText),
    },
    { id: 'park', label: t('calibrate.check.park'), ok: roof.parkConfirmed, text: deviceText(roof.parkText) },
    { id: 'fault', label: t('calibrate.check.fault'), ok: roof.fault === null, text: roof.fault ? deviceText(roof.fault.text) : '' },
    { id: 'sequence', label: t('calibrate.check.sequence'), ok: roof.closePhase === 'idle', text: '' },
    { id: 'still', label: t('calibrate.check.still'), ok: !moving, text: '' },
    { id: 'supply', label: t('calibrate.check.supply'), ok: !roof.supplyLow, text: '' },
    { id: 'hall', label: t('calibrate.check.hall'), ok: hallOk, text: '' },
  ];
};

/** Where the roof seems to be, as the start form's first guess (the person confirms it). */
export const whereFromState = (roof: RoofStatus | undefined): CalibrationWhere => {
  if (roof?.hall.level === 'low' || roof?.hall.level === 'high') return roof.state === 'open' ? 'open' : 'closed';
  return 'partWay';
};

/** Why Start can't be pressed now, or '' when it can. */
export const startBlocked = (checks: CalibrationCheck[], attended: boolean, live: boolean): string => {
  if (!live) return t('calibrate.reason.noLiveData');
  const failed = checks.find((check) => !check.ok);
  if (failed) return t('calibrate.reason.check', { check: failed.label });
  return attended ? '' : t('calibrate.reason.notAttended');
};

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
