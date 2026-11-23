import type { Config, LocationConfig, LocationSource, SecretState, SecretsProblem, SqmeterSite, SyncGroup } from '../types';
import type { ConfigPath } from '../components/settings/context';
import { bodyOf, postJson } from './api';
import { deviceError, deviceText } from '../i18n/deviceMessage';
import { formatCoordinates } from '../i18n/format';
import { t } from '../i18n';
import { COPIED_SECRET, isCopiedSecret } from './copiedSecret';

export { COPIED_SECRET } from './copiedSecret';

// Copy site settings from SQMeter (spec 020 US3, FR-014 to FR-018): the device
// fetches SQMeter's settings (POST /api/sqmeter/site); this lists each setting
// that differs, and the ticked rows become changes to the page's settings.
// Nothing is saved here: the normal Save does that.
// Never copied (FR-015): the device name, host name, Wi-Fi, Alpaca, OTA, the
// language. RoofRoof's own MQTT base topic is never changed. Spec 024: the MQTT
// password, ntfy token and Pushover keys SQMeter shares come across as
// COPIED_SECRET (the device fills them in at the save; the browser never sees
// them), and SQMeter's MQTT, ntfy and Pushover switches only ever turn RoofRoof's on.

export type CopyRowId =
  | 'location'
  | 'timezone'
  | 'ntp'
  | 'mqttBroker'
  | 'mqttPassword'
  | 'mqttEnabled'
  | 'weatherTopic'
  | 'ntfy'
  | 'ntfyToken'
  | 'ntfyEnabled'
  | 'pushoverKeys'
  | 'pushoverEnabled';
// What the person must enter after applying a row.
export type CopySecret = 'mqttPassword' | 'ntfyToken';
export type CopyNote = 'pushover';

export interface CopyRow {
  id: CopyRowId;
  ours: string; // RoofRoof's value, for display
  theirs: string; // SQMeter's value, for display
  changes: [ConfigPath, unknown][]; // null for a secret clears it on the device; COPIED_SECRET takes SQMeter's
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

// Spec 024: the device holds SQMeter's secret (it differs from RoofRoof's, or equals it).
const held = (state: SecretState | undefined) => state === 'received' || state === 'same';
const sharedOf = (site: SqmeterSite) => site.secrets;
const secretShown = (value: string | null | undefined) =>
  (value ?? '') === '' ? t('settings.sqmeterCopy.notSet') : t('settings.sqmeterCopy.secretSet');
const onOff = (on: boolean) => (on ? t('settings.sqmeterCopy.on') : t('settings.sqmeterCopy.off'));
// A row that turns a switch on (spec 024: a copy never turns one off).
// `needs`: what the switch can't be on without when RoofRoof lacks it (the broker, the topic, the keys),
// so no combination of ticks makes a save the device refuses.
const switchRow = (id: CopyRowId, path: ConfigPath, needs: [ConfigPath, unknown][] = []): CopyRow => ({
  id,
  ours: onOff(false),
  theirs: onOff(true),
  changes: [[path, true], ...needs],
  secrets: [],
});
// A row that takes SQMeter's received secret into `paths`.
const secretRow = (id: CopyRowId, ours: string | null | undefined, paths: ConfigPath[]): CopyRow => ({
  id,
  ours: secretShown(ours),
  theirs: t('settings.sqmeterCopy.received'),
  changes: paths.map((path): [ConfigPath, unknown] => [path, COPIED_SECRET]),
  secrets: [],
});

const brokerRow = (site: SqmeterSite, config: Config): CopyRow | null => {
  const mqtt = site.mqtt;
  const ours = config.mqtt;
  if (!mqtt || mqtt.broker.trim() === '') return null;
  if (mqtt.broker === ours.broker && mqtt.port === ours.port && mqtt.username === ours.username) return null;
  // The old password is never sent to a different broker (FR-017); SQMeter's goes with its own (spec 024).
  const copied = held(sharedOf(site)?.mqttPassword);
  return {
    id: 'mqttBroker',
    ours: broker(ours.broker, ours.port, ours.username),
    theirs: broker(mqtt.broker, mqtt.port, mqtt.username),
    changes: [
      [['mqtt', 'broker'], mqtt.broker],
      [['mqtt', 'port'], mqtt.port],
      [['mqtt', 'username'], mqtt.username],
      [['mqtt', 'password'], copied ? COPIED_SECRET : null],
    ],
    // Only when there is a password to enter: SQMeter's, or the one RoofRoof had.
    secrets: !copied && (mqtt.passwordSet || (ours.password ?? '') !== '') ? ['mqttPassword'] : [],
    asks: !copied && mqtt.passwordSet ? ['mqttPassword'] : [],
  };
};

// Spec 024: SQMeter's password for the broker RoofRoof already has, and its MQTT switch.
const mqttSecretRows = (site: SqmeterSite, config: Config): CopyRow[] => {
  const mqtt = site.mqtt;
  const ours = config.mqtt;
  if (!mqtt) return [];
  const rows: CopyRow[] = [];
  const sameBroker = mqtt.broker === ours.broker && mqtt.port === ours.port && mqtt.username === ours.username;
  if (sameBroker && sharedOf(site)?.mqttPassword === 'received' && !isCopiedSecret(ours.password))
    rows.push(secretRow('mqttPassword', ours.password, [['mqtt', 'password']]));
  if (mqtt.enabled && !ours.enabled && mqtt.broker.trim() !== '') {
    const needs = ours.broker.trim() === '' ? (brokerRow(site, config)?.changes ?? []) : [];
    rows.push(switchRow('mqttEnabled', ['mqtt', 'enabled'], needs));
  }
  return rows;
};

const mqttRows = (site: SqmeterSite, config: Config): CopyRow[] => {
  const mqtt = site.mqtt;
  if (!mqtt) return [];
  const rows: CopyRow[] = [brokerRow(site, config), ...mqttSecretRows(site, config)].filter((row): row is CopyRow => row !== null);
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

// The ntfy token for a new server: SQMeter's when shared (spec 024), else cleared (FR-017), and whether to ask for one.
const ntfyTokenChange = (site: SqmeterSite, ours: { token: string | null }, newServer: boolean) => {
  const copied = held(sharedOf(site)?.ntfyToken);
  const tokenSet = Boolean(site.ntfy?.tokenSet);
  return {
    changes: newServer ? [[['alerts', 'ntfy', 'token'], copied ? COPIED_SECRET : null] as [ConfigPath, unknown]] : [],
    secrets: !copied && (tokenSet || (newServer && (ours.token ?? '') !== '')) ? (['ntfyToken'] as CopySecret[]) : [],
    asks: !copied && tokenSet ? (['ntfyToken'] as CopySecret[]) : [],
  };
};

const ntfyRow = (site: SqmeterSite, config: Config): CopyRow | null => {
  const ntfy = site.ntfy;
  const ours = config.alerts?.ntfy;
  if (!ntfy || !ours || ntfy.topic.trim() === '') return null;
  const newServer = ntfy.server !== ours.server;
  if (!newServer && ntfy.topic === ours.topic) return null;
  const token = ntfyTokenChange(site, ours, newServer);
  return {
    id: 'ntfy',
    ours: ours.topic.trim() === '' ? t('settings.sqmeterCopy.notSet') : `${ours.server}/${ours.topic}`,
    theirs: `${ntfy.server}/${ntfy.topic}`,
    changes: [[['alerts', 'ntfy', 'server'], ntfy.server], [['alerts', 'ntfy', 'topic'], ntfy.topic], ...token.changes],
    secrets: token.secrets,
    asks: token.asks,
  };
};

// Spec 024: SQMeter's ntfy token for the server RoofRoof already has, and the channel's switch.
const ntfyRows = (site: SqmeterSite, config: Config): CopyRow[] => {
  const ntfy = site.ntfy;
  const ours = config.alerts?.ntfy;
  if (!ntfy || !ours) return [];
  const rows = [ntfyRow(site, config)].filter((row): row is CopyRow => row !== null);
  const topicSet = ntfy.topic.trim() !== '';
  if (topicSet && ntfy.server === ours.server && sharedOf(site)?.ntfyToken === 'received' && !isCopiedSecret(ours.token))
    rows.push(secretRow('ntfyToken', ours.token, [['alerts', 'ntfy', 'token']]));
  if (ntfy.enabled && !ours.enabled && topicSet) {
    const needs = ours.topic.trim() === '' ? (ntfyRow(site, config)?.changes ?? []) : [];
    rows.push(switchRow('ntfyEnabled', ['alerts', 'ntfy', 'enabled'], needs));
  }
  return rows;
};

// Spec 024: SQMeter's Pushover keys (both together) and the channel's switch.
const pushoverRows = (site: SqmeterSite, config: Config): CopyRow[] => {
  const theirs = site.pushover;
  const ours = config.alerts?.pushover;
  if (!theirs || !ours) return [];
  const rows: CopyRow[] = [];
  const state = sharedOf(site)?.pushover;
  if (state === 'received' && !isCopiedSecret(ours.userKey))
    rows.push(
      secretRow('pushoverKeys', ours.userKey, [
        ['alerts', 'pushover', 'userKey'],
        ['alerts', 'pushover', 'appToken'],
      ]),
    );
  const own = ours.userKey.trim() !== '' && ours.appToken.trim() !== '';
  if (theirs.enabled && !ours.enabled && (held(state) || own)) {
    const needs: [ConfigPath, unknown][] = own
      ? []
      : [
          [['alerts', 'pushover', 'userKey'], COPIED_SECRET],
          [['alerts', 'pushover', 'appToken'], COPIED_SECRET],
        ];
    rows.push(switchRow('pushoverEnabled', ['alerts', 'pushover', 'enabled'], needs));
  }
  return rows;
};

/** One row for each copyable setting that differs from the settings as edited (FR-016). */
// Spec 020: a copied group keeps following SQMeter (the location has its own source).
export const ROW_GROUP: Partial<Record<CopyRowId, SyncGroup>> = {
  timezone: 'time',
  ntp: 'time',
  mqttBroker: 'mqtt',
  mqttPassword: 'mqtt',
  mqttEnabled: 'mqtt',
  weatherTopic: 'mqtt',
  ntfy: 'alerts',
  ntfyToken: 'alerts',
  ntfyEnabled: 'alerts',
  pushoverKeys: 'alerts',
  pushoverEnabled: 'alerts',
};

const withFollow = (row: CopyRow, config: Config): CopyRow => {
  const group = ROW_GROUP[row.id];
  if (!group || !config.sqmeterSync) return row;
  return { ...row, changes: [...row.changes, [['sqmeterSync', group, 'follow'], true]] };
};

export const copyRows = (site: SqmeterSite, config: Config): CopyRow[] =>
  [
    locationRow(site, config),
    ...timeRows(site, config),
    ...mqttRows(site, config),
    ...ntfyRows(site, config),
    ...pushoverRows(site, config),
  ]
    .filter((row): row is CopyRow => row !== null)
    .map((row) => withFollow(row, config));

/** What can't be copied but is worth saying: SQMeter's Pushover keys, when it didn't share them. */
export const copyNotes = (site: SqmeterSite): CopyNote[] =>
  site.pushover && (site.pushover.userKeySet || site.pushover.appTokenSet) && !held(sharedOf(site)?.pushover) ? ['pushover'] : [];

/** Spec 024: what became of SQMeter's passwords: received (and whether over http), or why not. */
export type ShownSecretsProblem = Exclude<SecretsProblem, 'waiting'>;
export interface SecretsOutcome {
  received: boolean;
  http: boolean;
  problem: ShownSecretsProblem | null; // only when SQMeter has a secret set
}
export const secretsOutcome = (site: SqmeterSite): SecretsOutcome => {
  const secrets = site.secrets;
  if (!secrets) return { received: false, http: false, problem: null };
  const received = secrets.problem === null && [secrets.mqttPassword, secrets.ntfyToken, secrets.pushover].some(held);
  const wanted = Boolean(site.mqtt?.passwordSet || site.ntfy?.tokenSet || site.pushover?.userKeySet || site.pushover?.appTokenSet);
  // 'waiting' is the sync's, before its first read: nothing to say.
  const problem = wanted && secrets.problem !== 'waiting' ? secrets.problem : null;
  return { received, http: received && secrets.http, problem };
};

/** The ticked rows' changes, for the editor's applyChanges. */
export const changesOf = (rows: CopyRow[], ticked: ReadonlySet<CopyRowId>): [ConfigPath, unknown][] =>
  rows.filter((row) => ticked.has(row.id)).flatMap((row) => row.changes);
