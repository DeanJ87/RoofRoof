import type { LocationStatus } from '../types';
import { formatAgo, formatDateTime } from '../i18n/format';
import { t } from '../i18n';

// Where the daylight check's location came from and how old it is (spec 020
// FR-019, FR-020): one line for the dashboard's daylight row and Settings.
// It is never a safety verdict (FR-011): the tone only says whether a read failed.

export interface LocationLine {
  text: string;
  problem: boolean; // the last read failed (shown as a warning, never as unsafe)
  why?: string; // spec 034: with a kept location, the problem on its own (a "?" or a note of its own), never run on
}

// "read 3 min ago", "saved 19 Nov", or null when neither is known.
const whenOf = (location: LocationStatus): string | null => {
  if (location.readAgeSeconds !== null) return t('location.when.read', { ago: formatAgo(location.readAgeSeconds * 1000) });
  if (location.savedAt) {
    const date = formatDateTime(new Date(location.savedAt * 1000), { day: 'numeric', month: 'short' });
    return t('location.when.saved', { date });
  }
  return null;
};

/** The line for `location`, or null when there is nothing to say (a typed-in location not set). */
export const locationLine = (location: LocationStatus | undefined | null): LocationLine | null => {
  if (!location) return null;
  if (location.source === 'manual') return location.set ? { text: t('location.line.manual'), problem: false } : null;
  const when = whenOf(location);
  const problem = location.problem;
  if (problem && problem !== 'waiting') {
    const what = t(`location.problem.${problem}`);
    if (!location.set) return { text: what, problem: true };
    return { text: when ? t('location.line.using', { when }) : t('location.line.usingNoAge'), problem: true, why: what };
  }
  if (!location.set) return { text: t('location.problem.waiting'), problem: false };
  const from = location.gps ? 'gps' : 'sqmeter';
  return { text: when ? t(`location.line.${from}`, { when }) : t(`location.line.${from}NoAge`), problem: false };
};
