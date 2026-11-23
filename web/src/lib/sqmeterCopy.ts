import type { Config, LocationConfig, LocationSource, SqmeterSite, SyncGroup } from '../types';
import type { ConfigPath } from '../components/settings/context';
import { bodyOf, postJson } from './api';
import { deviceError, deviceText } from '../i18n/deviceMessage';
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
  asks?: CopySecret[]; // of `secrets`, those SQMeter has set: asked for at their fields after the copy
}

// `password`: the device couldn't log in to SQMeter (it wants one, or refused it).
export type SiteAnswer = { ok: true; site: SqmeterSite } | { ok: false; error: string; password?: boolean };

// The device's English text for SQMeter's 401 and 403 (spec 020).
const PASSWORD_ERROR = /^SQMeter (wants a|refused the) password$/;

/** SQMeter's login as edited: a masked or empty password means the saved one (the device decides). */
export interface SqmeterLogin {
  username?: string;
  password?: string | null;
}

// The body of the device's SQMeter routes (the copy and the Test) as the form stands: an emptied
// user name or a cleared password (null) is sent empty, so the device doesn't fall back to the
// saved login; an untouched mask means the saved password.
const sqmeterRequest = (url: string | undefined, login: SqmeterLogin): Record<string, string> => {
  const request: Record<string, string> = {};
  if (url) request.url = url;
  if (login.username !== undefined) request.username = login.username;
  if (login.password !== undefined) request.password = login.password ?? '';
  return request;
};

export type TestAnswer = { ok: true; safe: boolean; text: string } | { ok: false; error: string };

/** Spec 020 FR-038: one read of SQMeter's /api/safety at the typed address, through the device. */
export const testSqmeter = async (url: string, login: SqmeterLogin = {}): Promise<TestAnswer> => {
  try {
    const response = await postJson('/api/sqmeter/test', sqmeterRequest(url.trim() || undefined, login));
    const body = await bodyOf(response);
    if (response.ok && typeof body.safe === 'boolean') {
      const text = typeof body.text === 'string' ? deviceText(body.text) : '';
      return { ok: true, safe: body.safe, text };
    }
    return { ok: false, error: deviceError(body, t('settings.sqmeterCopy.failed')) };
  } catch {
    return { ok: false, error: t('settings.sqmeterCopy.unreachable') };
  }
};

/** Asks the device for SQMeter's site settings; `url` and `login` are as typed, saved or not. */
export const fetchSqmeterSite = async (url?: string, login: SqmeterLogin = {}): Promise<SiteAnswer> => {
  try {
    const response = await postJson('/api/sqmeter/site', sqmeterRequest(url, login));
    const body = await bodyOf(response);
    if (response.ok && body.device) return { ok: true, site: body as unknown as SqmeterSite };
    const password = typeof body.error === 'string' && PASSWORD_ERROR.test(body.error);
    return { ok: false, error: deviceError(body, t('settings.sqmeterCopy.failed')), password };
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
      asks: mqtt.passwordSet ? ['mqttPassword'] : [],
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
    asks: ntfy.tokenSet ? ['ntfyToken'] : [],
  };
};

/** One row for each copyable setting that differs from the settings as edited (FR-016). */
// Spec 020: a copied group keeps following SQMeter (the location has its own source).
export const ROW_GROUP: Partial<Record<CopyRowId, SyncGroup>> = {
  timezone: 'time',
  ntp: 'time',
  mqttBroker: 'mqtt',
  weatherTopic: 'mqtt',
  ntfy: 'alerts',
};

const withFollow = (row: CopyRow, config: Config): CopyRow => {
  const group = ROW_GROUP[row.id];
  if (!group || !config.sqmeterSync) return row;
  return { ...row, changes: [...row.changes, [['sqmeterSync', group, 'follow'], true]] };
};

export const copyRows = (site: SqmeterSite, config: Config): CopyRow[] =>
  [locationRow(site, config), ...timeRows(site, config), ...mqttRows(site, config), ntfyRow(site, config)]
    .filter((row): row is CopyRow => row !== null)
    .map((row) => withFollow(row, config));

/** What can't be copied but is worth saying: SQMeter's Pushover keys are never copied. */
export const copyNotes = (site: SqmeterSite): CopyNote[] =>
  site.pushover && (site.pushover.userKeySet || site.pushover.appTokenSet) ? ['pushover'] : [];

/** The ticked rows' changes, for the editor's applyChanges. */
export const changesOf = (rows: CopyRow[], ticked: ReadonlySet<CopyRowId>): [ConfigPath, unknown][] =>
  rows.filter((row) => ticked.has(row.id)).flatMap((row) => row.changes);
