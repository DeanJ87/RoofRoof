import type { Config, LocationConfig, LocationSource, SqmeterSite } from '../types';
import type { ConfigPath } from '../components/settings/context';
import { bodyOf, postJson } from './api';
import { deviceError } from '../i18n/deviceMessage';
import { formatCoordinates } from '../i18n/format';
import { t } from '../i18n';

// Copy site settings from SQMeter (spec 020 US3, FR-014 to FR-018): the device
// fetches SQMeter's settings (POST /api/sqmeter/site); this lists each setting
// that differs, and the ticked rows become changes to the page's settings.
// Nothing is saved here: the normal Save does that.
// Never copied (FR-015): the device name, host name, Wi-Fi, any password,
// token or key, Alpaca, OTA, the language, and the MQTT and alert channels'
// on/off switches. RoofRoof's own MQTT base topic is never changed.

export type CopyRowId = 'location' | 'timezone' | 'ntp' | 'mqttBroker' | 'weatherTopic' | 'ntfy';
// What the person must enter after applying a row.
export type CopySecret = 'mqttPassword' | 'ntfyToken';
export type CopyNote = 'pushover';

export interface CopyRow {
  id: CopyRowId;
  ours: string; // RoofRoof's value, for display
  theirs: string; // SQMeter's value, for display
  changes: [ConfigPath, unknown][]; // null for a secret clears it on the device
  secrets: CopySecret[];
}

export type SiteAnswer = { ok: true; site: SqmeterSite } | { ok: false; error: string };

/** Asks the device for SQMeter's site settings; `url` is an address typed but not yet saved. */
export const fetchSqmeterSite = async (url?: string): Promise<SiteAnswer> => {
  try {
    const response = await postJson('/api/sqmeter/site', url ? { url } : {});
    const body = await bodyOf(response);
    if (response.ok && body.device) return { ok: true, site: body as unknown as SqmeterSite };
    return { ok: false, error: deviceError(body, t('settings.sqmeterCopy.failed')) };
  } catch {
    return { ok: false, error: t('settings.sqmeterCopy.unreachable') };
  }
};

/** The location's source; settings from before spec 020 follow SQMeter only when no location was typed in. */
export const locationSource = (location: LocationConfig): LocationSource => location.source ?? (location.set ? 'manual' : 'sqmeter');

const shown = (value: string) => (value.trim() === '' ? t('settings.sqmeterCopy.notSet') : value);
const servers = (a: string, b: string) => shown([a, b].filter((s) => s.trim() !== '').join(', '));
const broker = (host: string, port: number, user: string) =>
  host.trim() === '' ? t('settings.sqmeterCopy.notSet') : `${user ? `${user}@` : ''}${host}:${port}`;

const locationRow = (site: SqmeterSite, config: Config): CopyRow | null => {
  const ours = config.location;
  const theirs = site.location;
  // Offered whenever SQMeter has a location section: with GPS only, its typed one may be unset
  // while the device would still read its live fix.
  if (!ours || !theirs || locationSource(ours) !== 'manual') return null;
  const changes: [ConfigPath, unknown][] = [[['location', 'source'], 'sqmeter']];
  // Its typed location until the device's first read, when it has one.
  if (theirs.set)
    changes.push([['location', 'set'], true], [['location', 'latitude'], theirs.latitude], [['location', 'longitude'], theirs.longitude]);
  return {
    id: 'location',
    ours: ours.set ? formatCoordinates(ours.latitude, ours.longitude, 4) : t('settings.sqmeterCopy.notSet'),
    theirs: theirs.set ? formatCoordinates(theirs.latitude, theirs.longitude, 4) : t('settings.sqmeterCopy.liveLocation'),
    changes,
    secrets: [],
  };
};

const timeRows = (site: SqmeterSite, config: Config): CopyRow[] => {
  const time = site.time;
  if (!time) return [];
  const rows: CopyRow[] = [];
  if (time.timezone.trim() !== '' && time.timezone !== config.ntp.timezone)
    rows.push({
      id: 'timezone',
      ours: shown(config.ntp.timezone),
      theirs: time.timezone,
      changes: [[['ntp', 'timezone'], time.timezone]],
      secrets: [],
    });
  const anyServer = time.server1.trim() !== '' || time.server2.trim() !== '';
  if (anyServer && (time.server1 !== config.ntp.server1 || time.server2 !== config.ntp.server2))
    rows.push({
      id: 'ntp',
      ours: servers(config.ntp.server1, config.ntp.server2),
      theirs: servers(time.server1, time.server2),
      changes: [
        [['ntp', 'server1'], time.server1],
        [['ntp', 'server2'], time.server2],
      ],
      secrets: [],
    });
  return rows;
};

const mqttRows = (site: SqmeterSite, config: Config): CopyRow[] => {
  const mqtt = site.mqtt;
  if (!mqtt) return [];
  const rows: CopyRow[] = [];
  const ours = config.mqtt;
  const differs = mqtt.broker !== ours.broker || mqtt.port !== ours.port || mqtt.username !== ours.username;
  if (mqtt.broker.trim() !== '' && differs)
    rows.push({
      id: 'mqttBroker',
      ours: broker(ours.broker, ours.port, ours.username),
      theirs: broker(mqtt.broker, mqtt.port, mqtt.username),
      // The old password is never sent to a different broker (FR-017).
      changes: [
        [['mqtt', 'broker'], mqtt.broker],
        [['mqtt', 'port'], mqtt.port],
        [['mqtt', 'username'], mqtt.username],
        [['mqtt', 'password'], null],
      ],
      // Only when there is a password to enter: SQMeter's, or the one RoofRoof had.
      secrets: mqtt.passwordSet || (ours.password ?? '') !== '' ? ['mqttPassword'] : [],
    });
  // SQMeter's base topic is where the MQTT weather source reads <base>/safe.
  const safety = config.safety;
  if (safety && mqtt.topic.trim() !== '' && mqtt.topic !== safety.mqtt.topic)
    rows.push({
      id: 'weatherTopic',
      ours: shown(safety.mqtt.topic),
      theirs: mqtt.topic,
      changes: [[['safety', 'mqtt', 'topic'], mqtt.topic]],
      secrets: [],
    });
  return rows;
};

const ntfyRow = (site: SqmeterSite, config: Config): CopyRow | null => {
  const ntfy = site.ntfy;
  const ours = config.alerts?.ntfy;
  if (!ntfy || !ours || ntfy.topic.trim() === '') return null;
  const newServer = ntfy.server !== ours.server;
  if (!newServer && ntfy.topic === ours.topic) return null;
  const changes: [ConfigPath, unknown][] = [
    [['alerts', 'ntfy', 'server'], ntfy.server],
    [['alerts', 'ntfy', 'topic'], ntfy.topic],
  ];
  // The old token is never sent to a different server (FR-017).
  if (newServer) changes.push([['alerts', 'ntfy', 'token'], null]);
  return {
    id: 'ntfy',
    ours: ours.topic.trim() === '' ? t('settings.sqmeterCopy.notSet') : `${ours.server}/${ours.topic}`,
    theirs: `${ntfy.server}/${ntfy.topic}`,
    changes,
    secrets: ntfy.tokenSet || (newServer && (ours.token ?? '') !== '') ? ['ntfyToken'] : [],
  };
};

/** One row for each copyable setting that differs from the settings as edited (FR-016). */
export const copyRows = (site: SqmeterSite, config: Config): CopyRow[] =>
  [locationRow(site, config), ...timeRows(site, config), ...mqttRows(site, config), ntfyRow(site, config)].filter(
    (row): row is CopyRow => row !== null,
  );

/** What can't be copied but is worth saying: SQMeter's Pushover keys are never copied. */
export const copyNotes = (site: SqmeterSite): CopyNote[] =>
  site.pushover && (site.pushover.userKeySet || site.pushover.appTokenSet) ? ['pushover'] : [];

/** The ticked rows' changes, for the editor's applyChanges. */
export const changesOf = (rows: CopyRow[], ticked: ReadonlySet<CopyRowId>): [ConfigPath, unknown][] =>
  rows.filter((row) => ticked.has(row.id)).flatMap((row) => row.changes);
