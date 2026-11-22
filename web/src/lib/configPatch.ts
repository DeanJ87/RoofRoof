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
];

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

// `paths` with the credential partners of any of them.
const withPartners = (paths: string[]): string[] => [
  ...new Set([...paths, ...CREDENTIAL_PARTNERS.filter((group) => group.some((path) => paths.includes(path))).flat()]),
];

/** The loaded value (null when the device didn't send it) of every field the patch changes and of its partners. */
export const baseValuesFor = (loaded: unknown, patch: unknown): Node => {
  const base: Node = {};
  withPartners(leafPaths(patch)).forEach((path) => setAt(base, path, valueAt(loaded, path) ?? null));
  return base;
};

/** `patch` written over a copy of `target`. */
export const applyPatch = <T>(target: T, patch: unknown): T => {
  if (!isObject(patch)) return (patch === undefined ? target : patch) as T;
  const out: Node = isObject(target) ? { ...target } : {};
  Object.entries(patch).forEach(([key, value]) => {
    out[key] = applyPatch(out[key], value);
  });
  return out as T;
};

const overlaps = (path: string, field: string) => path === field || path.startsWith(`${field}.`) || field.startsWith(`${path}.`);

/**
 * After a refused save: the person's edits (`edited` against `loaded`) over the device's settings,
 * except those to the changed fields and their credential partners (a password typed for a
 * network that changed is dropped with it).
 */
export const rebaseEdits = <T>(fresh: T, loaded: T, edited: T, changedFields: string[]): { config: T; replaced: string[] } => {
  const changed = withPartners(changedFields);
  const edits = diffConfig(loaded, edited);
  const kept: Node = {};
  const replaced: string[] = [];
  leafPaths(edits).forEach((path) => {
    if (changed.some((field) => overlaps(path, field))) replaced.push(path);
    else setAt(kept, path, valueAt(edits, path));
  });
  return { config: applyPatch(fresh, kept), replaced };
};
