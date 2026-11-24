import type { Config, SecretsProblem, SyncGroup, SyncNeed, SyncStatus } from '../types';
import { formatAgo, formatDateTime } from '../i18n/format';
import { t } from '../i18n';
import { MEASURED_FIELDS, SYNC_GROUP_FIELDS } from './configPatch';

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

/** The group follows SQMeter and has synced from it at least once. */
export const synced = (config: Config, group: SyncGroup): boolean =>
  follows(config, group) && Boolean(config.sqmeterSync?.[group]?.syncedAt);

/**
 * Spec 020: what a copy from SQMeter brought in this page load and its field must show for, also
 * with the channel off: a secret SQMeter has set (to be typed here), or SQMeter's weather topic.
 */
export type CopyShown = SyncNeed | 'weatherTopic';
const copyShown = new Set<CopyShown>();
export const rememberCopy = (items: CopyShown[]) => items.forEach((item) => copyShown.add(item));
export const forgetCopy = () => copyShown.clear();
export const fromCopy = (item: CopyShown): boolean => copyShown.has(item);

/** The device (a following group) or the last copy asks for `need`. */
export const asked = (sync: SyncStatus | null | undefined, need: SyncNeed): boolean => needs(sync, need) || fromCopy(need);

/**
 * Spec 020: MQTT's fields show while MQTT is off too when they hold something: a broker (copied
 * from SQMeter, say), a group that synced from SQMeter, or a password to enter. Following alone
 * (a new device's default) isn't enough: an owner without MQTT never sees them then.
 */
export const mqttShown = (config: Config, sync: SyncStatus | null | undefined): boolean =>
  config.mqtt.enabled ||
  config.mqtt.broker.trim() !== '' ||
  synced(config, 'mqtt') ||
  config.mqtt.password === null ||
  asked(sync, 'mqttPassword');

/** The same for ntfy's server, topic and token while the channel is off. */
export const ntfyShown = (config: Config, sync: SyncStatus | null | undefined): boolean => {
  const ntfy = config.alerts?.ntfy;
  if (!ntfy) return false;
  return ntfy.enabled || ntfy.topic.trim() !== '' || synced(config, 'alerts') || ntfy.token === null || asked(sync, 'ntfyToken');
};

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

/** Spec 024: the groups whose secrets and switches the sync keeps too. */
export const keepsSecrets = (group: SyncGroup): boolean => group === 'mqtt' || group === 'alerts';

/** Spec 024: why the sync couldn't read SQMeter's passwords, for a following MQTT or alerts group. */
export const secretsProblem = (sync: SyncStatus | null | undefined, group: SyncGroup): Exclude<SecretsProblem, 'waiting'> | null => {
  const secrets = sync?.secrets;
  if (!keepsSecrets(group) || !sync?.[group]?.follow || !secrets || secrets === 'received' || secrets === 'waiting') return null;
  return secrets;
};

const SECRET_NEED: Record<string, SyncNeed> = {
  'mqtt.password': 'mqttPassword',
  'alerts.ntfy.token': 'ntfyToken',
  'alerts.pushover.userKey': 'pushoverKeys',
  'alerts.pushover.appToken': 'pushoverKeys',
};

/**
 * Spec 026: a person's edit, plus "stop following" for the SQMeter group it belongs to while that group
 * follows: typing over a value SQMeter sets makes the group set here, never silently overwritten later.
 * A secret the device asks for (SQMeter has one RoofRoof lacks) only fills the gap: the group keeps following.
 */
export const editChanges = (config: Config | null, path: string[], value: unknown, sync?: SyncStatus | null): [string[], unknown][] => {
  const dotted = path.join('.');
  const group = (Object.keys(SYNC_GROUP_FIELDS) as SyncGroup[]).find((name) =>
    SYNC_GROUP_FIELDS[name].some((field) => dotted === field || dotted.startsWith(`${field}.`)),
  );
  const need = SECRET_NEED[dotted];
  const filling = need !== undefined && asked(sync, need);
  const changes: [string[], unknown][] = [[path, value]];
  if (group && config && follows(config, group) && !filling) changes.push([['sqmeterSync', group, 'follow'], false]);
  // A measured roof value typed over is no longer measured (as the device's rule).
  const bit = MEASURED_FIELDS[dotted];
  const measured = config?.roof?.measured;
  if (bit && measured !== undefined && measured & bit) changes.push([['roof', 'measured'], measured & ~bit]);
  return changes;
};

/** Several edits at once with editChanges' rule; their measured bits combine (each clears its own). */
export const editBatch = (config: Config | null, changes: [string[], unknown][], sync?: SyncStatus | null): [string[], unknown][] => {
  const all = changes.flatMap(([path, value]) => editChanges(config, path, value, sync));
  const isMeasured = ([path]: [string[], unknown]) => path.join('.') === 'roof.measured';
  const bits = all.filter(isMeasured).map(([, value]) => value as number);
  if (bits.length < 2) return all;
  return [...all.filter((change) => !isMeasured(change)), [['roof', 'measured'], bits.reduce((a, b) => a & b)]];
};
