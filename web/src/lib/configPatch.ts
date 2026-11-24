// Spec 021: a settings save sends only what the person changed, with the
// values the page loaded for those fields (and their credential partners), so
// the device can refuse it when any of them changed meanwhile, and keep every
// other change made elsewhere.

type Node = Record<string, unknown>;

const isObject = (value: unknown): value is Node => typeof value === 'object' && value !== null && !Array.isArray(value);

const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

/** The parts of `next` that differ from `before`: objects walked, anything else compared whole. Undefined when equal. */
export const diffConfig = (before: unknown, next: unknown): unknown => {
  if (!isObject(before) || !isObject(next)) return next === undefined || same(before, next) ? undefined : next;
  const out: Node = {};
  for (const key of Object.keys(next)) {
    const part = diffConfig(before[key], next[key]);
    if (part !== undefined) out[key] = part;
  }
  return Object.keys(out).length > 0 ? out : undefined;
};

/** Dotted names of the leaves of a patch. */
export const leafPaths = (patch: unknown, prefix = ''): string[] =>
  isObject(patch)
    ? Object.entries(patch).flatMap(([key, value]) => leafPaths(value, prefix ? `${prefix}.${key}` : key))
    : prefix
      ? [prefix]
      : [];

/** Fields that only make sense together: a password goes with the network or server it was typed for. */
export const CREDENTIAL_PARTNERS: string[][] = [
  ['wifi.ssid', 'wifi.password'],
  ['mqtt.broker', 'mqtt.port', 'mqtt.username', 'mqtt.password'],
  ['auth.username', 'auth.password'],
  ['alerts.ntfy.server', 'alerts.ntfy.topic', 'alerts.ntfy.token'],
  ['alerts.pushover.userKey', 'alerts.pushover.appToken'],
  ['alerts.webhook.url', 'alerts.webhook.authHeader'],
  ['safety.http.url', 'safety.http.username', 'safety.http.password'],
];

/**
 * Spec 024: fields always sent together when either changes: a save with `park.switches` alone makes
 * the device derive the park source from it (an older page's meaning), so the source goes with it.
 */
export const SENT_TOGETHER: string[][] = [['park.source', 'park.switches']];

const valueAt = (source: unknown, path: string): unknown =>
  path.split('.').reduce<unknown>((node, key) => (isObject(node) ? node[key] : undefined), source);

const setAt = (target: Node, path: string, value: unknown) => {
  const keys = path.split('.');
  let node = target;
  keys.slice(0, -1).forEach((key) => {
    if (!isObject(node[key])) node[key] = {};
    node = node[key] as Node;
  });
  node[keys[keys.length - 1]] = value;
};

// `paths` with the credential partners (and the fields sent together) of any of them.
const withPartners = (paths: string[]): string[] => [
  ...new Set([
    ...paths,
    ...[...CREDENTIAL_PARTNERS, ...SENT_TOGETHER].filter((group) => group.some((path) => paths.includes(path))).flat(),
  ]),
];

/** `path` is `field`, inside it, or holds it. */
const overlaps = (path: string, field: string) => path === field || path.startsWith(`${field}.`) || field.startsWith(`${path}.`);

/**
 * Spec 026: the values each group that can follow SQMeter covers. A save that changes one of them
 * says whether its group still follows, so the device never guesses: a person's edit sends `false`
 * (typing unfollows), a copy from SQMeter sends `true`; a save that says nothing (a script) unfollows.
 */
export const SYNC_GROUP_FIELDS: Record<'time' | 'mqtt' | 'alerts', string[]> = {
  time: ['ntp.timezone', 'ntp.server1', 'ntp.server2'],
  mqtt: ['mqtt.broker', 'mqtt.port', 'mqtt.username', 'mqtt.password', 'mqtt.enabled', 'safety.mqtt.topic'],
  alerts: [
    'alerts.ntfy.server',
    'alerts.ntfy.topic',
    'alerts.ntfy.token',
    'alerts.ntfy.enabled',
    'alerts.pushover.userKey',
    'alerts.pushover.appToken',
    'alerts.pushover.enabled',
  ],
};

/** Spec 026: each roof value the device marks as measured (RoofConfig::MEASURED_* bits). */
export const MEASURED_FIELDS: Record<string, number> = {
  'roof.travelMm': 1,
  'roof.speedMmPerSecond': 2,
  'roof.timeoutMarginSeconds': 4,
  'roof.hallHighEnd': 8,
  'roof.hallHighCounts': 8,
  'roof.hallLowCounts': 8,
  'roof.openDirection': 16,
};

/** The follow flags of the sync groups whose values `paths` change. */
const followPaths = (paths: string[]): string[] =>
  Object.entries(SYNC_GROUP_FIELDS)
    .filter(([, fields]) => paths.some((path) => fields.some((field) => overlaps(path, field))))
    .map(([group]) => `sqmeterSync.${group}.follow`);

/**
 * `patch` with the other fields of a SENT_TOGETHER group from `full` (the settings as saved) when one of
 * them is in it, and the follow flag of each SQMeter group whose values it changes (spec 026).
 */
export const withSentTogether = (patch: unknown, full: unknown): unknown => {
  const paths = leafPaths(patch);
  // A measured roof value goes with `roof.measured` as edited, so the device never guesses (spec 026).
  const measured = paths.some((path) => path in MEASURED_FIELDS) ? ['roof.measured'] : [];
  return [...SENT_TOGETHER.filter((group) => group.some((path) => paths.includes(path))).flat(), ...followPaths(paths), ...measured]
    .filter((path) => !paths.includes(path) && valueAt(full, path) !== undefined)
    .reduce((out, path) => applyPatch(out, pathPatch(path, valueAt(full, path))), patch);
};

/**
 * The loaded value (null when the device didn't send it) of every field the patch changes and of
 * its partners. Not the setup wizard's progress (`setup`): the page owns it, and Next on an
 * unchanged screen saves it on its own.
 */
export const baseValuesFor = (loaded: unknown, patch: unknown): Node => {
  const base: Node = {};
  withPartners(leafPaths(patch))
    .filter((path) => !path.startsWith('setup.') && path !== 'setup')
    .forEach((path) => setAt(base, path, valueAt(loaded, path) ?? null));
  return base;
};

const pathPatch = (path: string, value: unknown): Node => {
  const out: Node = {};
  setAt(out, path, value);
  return out;
};

const SECRET_KEYS = ['password', 'token', 'userKey', 'appToken', 'authHeader'];
const MASK = '********';

/**
 * A saved patch as the device now shows it: a secret set is masked, a secret cleared (null) is "",
 * and a placeholder ("", the mask) kept the secret the page loaded.
 */
export const asShown = (patch: unknown, loaded: unknown): unknown => {
  let shown = patch;
  leafPaths(patch)
    .filter((path) => SECRET_KEYS.includes(path.split('.').pop() ?? ''))
    .forEach((path) => {
      const value = valueAt(patch, path);
      const kept = value === '' || value === '***' || value === MASK;
      const now = value === null ? '' : kept ? (valueAt(loaded, path) ?? '') : MASK;
      shown = applyPatch(shown, pathPatch(path, now));
    });
  return shown;
};

/** The device names at most this many changed fields (lib/ConfigModel ConfigSave MAX_CONFLICTS). */
export const MAX_CONFLICTS = 16;

/** `patch` written over a copy of `target`. */
export const applyPatch = <T>(target: T, patch: unknown): T => {
  if (!isObject(patch)) return (patch === undefined ? target : patch) as T;
  const out: Node = isObject(target) ? { ...target } : {};
  Object.entries(patch).forEach(([key, value]) => {
    out[key] = applyPatch(out[key], value);
  });
  return out as T;
};

/**
 * After a refused save: the person's edits (`edited` against `loaded`) over the device's settings,
 * except those to the changed fields and their credential partners (a password typed for a
 * network that changed is dropped with it).
 */
export const rebaseEdits = <T>(fresh: T, loaded: T, edited: T, changedFields: string[]): { config: T; replaced: string[] } => {
  const changed = withPartners(changedFields);
  const edits = diffConfig(loaded, edited);
  // No names, or as many as the device lists: the list may be incomplete, so nothing is kept.
  const unknown = changedFields.length === 0 || changedFields.length >= MAX_CONFLICTS;
  const kept: Node = {};
  const replaced: string[] = [];
  leafPaths(edits).forEach((path) => {
    if (unknown || changed.some((field) => overlaps(path, field))) replaced.push(path);
    else setAt(kept, path, valueAt(edits, path));
  });
  return { config: applyPatch(fresh, kept), replaced };
};
