import type { SetupConfig } from '../../types';
import type { SaveOutcome } from '../../hooks/useConfigEditor';
import type { DepEntry } from '../../lib/settingsDeps';
import { fieldErrorAliases } from '../settings/payload';
import { listReasons } from '../settings/restart';
import { clampStep } from './pages';
import { firstScreenOf, screenIndex, type ScreenId } from './screens';
import { t } from '../../i18n';

// Where the wizard opens and what a refused save says (spec 025).

/**
 * The screen to open, and whether the address asked for it (`?screen=<id>`, or `?step=N` for
 * part N from System's "Run setup wizard"). An explicit start applies once: the wizard then
 * replaces the address, so a reload resumes. Without one, the wizard resumes at the saved part in
 * every setup state (a re-run from System included).
 */
export const startFrom = (setup: SetupConfig | undefined, query: URLSearchParams): { index: number; explicit: boolean } => {
  const screen = screenIndex(query.get('screen') ?? '');
  if (screen >= 0) return { index: screen, explicit: true };
  if (query.has('step')) return { index: firstScreenOf(clampStep(Number(query.get('step')) - 1)), explicit: true };
  // Finish and Skip setup save part 0, so a finished setup opens at the start.
  return { index: firstScreenOf(clampStep(setup?.step ?? 0)), explicit: false };
};

const fieldOnScreen = (key: string): boolean => {
  const alias = Object.entries(fieldErrorAliases).find(([, path]) => path === key)?.[0];
  return Boolean(document.querySelector(`[data-field="${key}"]`) ?? (alias && document.querySelector(`[data-field="${alias}"]`)));
};

/**
 * FR-023: a refused save's note. Each error on the screen sits under its field; the note names
 * any that aren't on this screen (their messages name the field), or says to fix those marked.
 */
export const invalidText = (errors: Record<string, string>): string => {
  const elsewhere = Object.entries(errors)
    .filter(([key]) => !fieldOnScreen(key))
    .map(([, message]) => message);
  return elsewhere.length > 0 ? t('wizard.fixFields', { fields: listReasons(elsewhere) }) : t('wizard.fixMarked');
};

export const outcomeText = (outcome: SaveOutcome): string | null => {
  if (outcome.kind === 'invalid') return invalidText(outcome.errors);
  if (outcome.kind === 'refused') return outcome.message;
  if (outcome.kind === 'unreachable') return t('settings.couldNotReachTheDevice');
  if (outcome.kind === 'conflict') return t('wizard.changedOnDevice', { fields: listReasons(outcome.fields) });
  return null;
};

export const focusField = (errors: Record<string, string>) => {
  const first = Object.keys(errors)[0];
  if (!first) return;
  const alias = Object.entries(fieldErrorAliases).find(([, path]) => path === first)?.[0];
  setTimeout(() => {
    const target =
      document.querySelector<HTMLElement>(`[data-field="${first}"]`) ??
      (alias ? document.querySelector<HTMLElement>(`[data-field="${alias}"]`) : null);
    target?.focus?.();
  }, 60);
};

// FR-007: the fixes the wizard makes in place: a setting turned on, or the screen that sets it.
const TURN_ON: Record<string, string[]> = {
  'mqtt-off': ['mqtt', 'enabled'],
  'alerts-off': ['alerts', 'enabled'],
  'alpaca-off': ['alpaca', 'enabled'],
};
const OPEN_SCREEN: Record<string, ScreenId> = { 'location-unknown': 'site', 'no-sqmeter-address': 'sqmeter', 'clock-not-set': 'site' };

export type InPlaceFix = { turnOn: string[] } | { screen: ScreenId } | null;

/** What a dependency fix does inside the wizard; null: only Settings can (save, then leave). */
export const inPlaceFix = (entry: DepEntry): InPlaceFix => {
  const reason = entry.reason ?? entry.blockedBy?.reason ?? '';
  if (TURN_ON[reason]) return { turnOn: TURN_ON[reason] };
  return OPEN_SCREEN[reason] ? { screen: OPEN_SCREEN[reason] } : null;
};
