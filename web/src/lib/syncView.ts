import type { Config, SyncGroup, SyncNeed, SyncStatus } from '../types';
import { formatAgo, formatDateTime } from '../i18n/format';
import { t } from '../i18n';

// Spec 020: whether a group of site settings follows SQMeter, and the one line
// that says how that is going. Never a safety verdict: a failed read only
// means the values in effect are the last ones synced.

/** The group follows SQMeter in the settings as edited (older firmware: never). */
export const follows = (config: Config, group: SyncGroup): boolean => config.sqmeterSync?.[group]?.follow ?? false;

/**
 * The group's fields are read-only: it follows SQMeter, SQMeter's address is set, and (MQTT) roof
 * commands are off. Otherwise the device never syncs it, so the person's values stand.
 */
export const locked = (config: Config, group: SyncGroup): boolean =>
  follows(config, group) && Boolean(config.safety?.http.url.trim()) && !(group === 'mqtt' && config.mqtt.roofCommands);

/** The group a secret belongs to. */
export const NEED_GROUP: Record<SyncNeed, SyncGroup> = { mqttPassword: 'mqtt', ntfyToken: 'alerts', pushoverKeys: 'alerts' };

/** The device asks for `need` (SQMeter has it set, RoofRoof hasn't). */
export const needs = (sync: SyncStatus | null | undefined, need: SyncNeed): boolean =>
  Boolean(sync?.[NEED_GROUP[need]]?.needs.includes(need));

export interface SyncLine {
  text: string;
  problem: boolean; // the last read of SQMeter's settings failed
}

/** "Follows SQMeter, checked 3 min ago", the read's problem, or that it isn't synced yet. */
export const syncLine = (sync: SyncStatus | null | undefined, group: SyncGroup): SyncLine => {
  const live = sync?.[group];
  if (!sync || !live) return { text: t('sync.follows'), problem: false };
  // Switched to follow but not saved yet: the device starts once it is.
  if (!live.follow) return { text: t('sync.onceSaved'), problem: false };
  if (sync.problem && sync.problem !== 'waiting')
    return { text: t('sync.followsProblem', { problem: t(`location.problem.${sync.problem}`) }), problem: true };
  if (sync.readAgeSeconds !== null) return { text: t('sync.checked', { ago: formatAgo(sync.readAgeSeconds * 1000) }), problem: false };
  if (live.syncedAt) {
    const date = formatDateTime(new Date(live.syncedAt * 1000), { day: 'numeric', month: 'short' });
    return { text: t('sync.synced', { date }), problem: false };
  }
  return { text: t('sync.notYet'), problem: false };
};
