import type { MessageKey } from '../../i18n';
import type { RoofConfig } from '../../types';

// Spec 036: the end and movement checks' settings, shown in the Motion card
// (ranges in roofSchema.ts), and the slow duty below which the roof may crawl
// with no supply sag (RoofConfig::ADVISED_SLOW_DUTY_PERCENT).
export const ADVISED_SLOW_DUTY_PERCENT = 40;

export const MOTION_CHECKS: { key: keyof RoofConfig; label: MessageKey; hint: MessageKey; unit: string; integer: boolean }[] = [
  { key: 'endPlausiblePercent', label: 'settings.roof.endPlausible', hint: 'settings.roof.endPlausibleHint', unit: '%', integer: true },
  { key: 'endOverrunPercent', label: 'settings.roof.endOverrun', hint: 'settings.roof.endOverrunHint', unit: '%', integer: true },
  { key: 'motionSagVolts', label: 'settings.roof.motionSag', hint: 'settings.roof.motionSagHint', unit: 'V', integer: false },
  { key: 'motionCheckSeconds', label: 'settings.roof.motionCheck', hint: 'settings.roof.motionCheckHint', unit: 's', integer: true },
];
