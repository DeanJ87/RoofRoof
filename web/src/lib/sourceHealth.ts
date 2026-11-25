import type { RoofStatus } from '../types';
import { deviceText } from '../i18n/deviceMessage';
import { formatAgo, formatCount } from '../i18n/format';
import { t } from '../i18n';
import { minSec } from './roofView';

// Spec 027 (FR-015, US4, US5): SQMeter's and N.I.N.A.'s health as the device reports it, in one
// line each, for the Roof card's weather and park rows and the Diagnostics page. `ok` is false for
// a problem worth a note; null when there is nothing to say (the source is off or not set).

export interface HealthLine {
  ok: boolean;
  text: string;
}

const span = (seconds: number) => (seconds < 60 ? t('units.seconds', { n: formatCount(seconds) }) : minSec(seconds));

export const sqmeterLine = (roof: RoofStatus | null | undefined): HealthLine | null => {
  const sqm = roof?.sqmeter;
  if (!sqm?.on) return null;
  const age = sqm.lastReplySeconds;
  if (sqm.state === 'good' && age !== null) return { ok: true, text: t('roof.health.sqmReplied', { ago: formatAgo(age * 1000) }) };
  if (sqm.state === 'waiting' && age === null) return null; // just started: nothing asked yet
  const text = deviceText(sqm.text) || sqm.state;
  return { ok: false, text: age === null ? t('roof.health.sqmNever', { text }) : t('roof.health.sqmSilent', { age: span(age), text }) };
};

export const ninaLine = (roof: RoofStatus | null | undefined): HealthLine | null => {
  const nina = roof?.nina;
  if (!nina?.set || nina.state === 'waiting') return null;
  if (nina.reachable) return { ok: true, text: t('roof.health.ninaAnswered', { ago: formatAgo(nina.ageSeconds * 1000) }) };
  return { ok: false, text: t('roof.health.ninaLost', { text: deviceText(nina.text) || nina.state }) };
};
