import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import type { Config, SystemStatus } from '../types';
import { getConfigValidationErrors, hasConfigValidationErrors, type ValidationErrors } from '../validation/configSchema';
import type { ConfigPath } from '../components/settings/context';
import { toConfigPayload } from '../components/settings/payload';
import { restartReasons } from '../components/settings/restart';
import { fetchEffectiveReport, type EffectiveReport } from '../lib/settingsDeps';
import { fetchConfigWithRevision, fetchStatus, saveConfig } from '../lib/settingsApi';
import { applyPatch, asShown, baseValuesFor, diffConfig, rebaseEdits } from '../lib/configPatch';
import { deviceError } from '../i18n/deviceMessage';
import { t } from '../i18n';

// The settings as edited in the browser and saved to the device: what
// Settings and the setup wizard (spec 015) share, so both load, validate and
// save the same way (the web UI's schema, then the device's own checks,
// which refuse a save while the roof moves).

const STATUS_REFRESH_MS = 10000;

// Sets value at path, copying every object along the way so state is never
// mutated in place.
type Node = Record<string, unknown>;
const setPath = (target: Config, path: ConfigPath, value: unknown): Config => {
  const root: Node = { ...target };
  let current = root;
  for (let i = 0; i < path.length - 1; i++) {
    current[path[i]] = { ...(current[path[i]] as Node) };
    current = current[path[i]] as Node;
  }
  current[path[path.length - 1]] = value;
  return root as unknown as Config;
};

export type SaveOutcome =
  | { kind: 'invalid'; errors: ValidationErrors }
  | { kind: 'saved'; payload: Config; restart: string[] }
  | { kind: 'refused'; message: string }
  // Spec 021: settings this page changed were changed on the device since it loaded them.
  | { kind: 'conflict'; fields: string[] }
  | { kind: 'unreachable' };

// What the page loaded: the device's own document and its revision (spec 021).
type Loaded = { document: unknown; revision: number | undefined };
type Submitted = { outcome: SaveOutcome; patch: unknown; revision?: number };

const conflictsOf = (body: { conflicts?: unknown } | null): string[] | null =>
  Array.isArray(body?.conflicts) ? body.conflicts.filter((name): name is string => typeof name === 'string') : null;

// Validates `config` (with `extra` added) and saves only what differs from
// `saved`, with the loaded values of those fields when the device sent a revision.
const submit = async (config: Config, saved: Config, extra: Partial<Config>, loaded: Loaded): Promise<Submitted> => {
  const payload = toConfigPayload(config);
  const full = { ...payload, ...extra };
  const errors = getConfigValidationErrors(full, saved);
  if (hasConfigValidationErrors(errors)) return { outcome: { kind: 'invalid', errors }, patch: undefined };
  const patch = diffConfig(saved, full) ?? {};
  const base = loaded.revision === undefined ? {} : { baseRevision: loaded.revision, baseValues: baseValuesFor(loaded.document, patch) };
  try {
    const { ok, body } = await saveConfig({ ...(patch as object), ...base });
    const fields = ok ? null : conflictsOf(body);
    if (fields) return { outcome: { kind: 'conflict', fields }, patch };
    if (!ok || body?.success === false)
      return { outcome: { kind: 'refused', message: deviceError(body, t('settings.failedToSaveSettings')) }, patch };
    const revision = typeof body?.revision === 'number' ? body.revision : undefined;
    return { outcome: { kind: 'saved', payload, restart: restartReasons(saved, payload) }, patch, revision };
  } catch {
    return { outcome: { kind: 'unreachable' }, patch };
  }
};

// The live status and what is in effect, refreshed every 10 s.
const useDeviceState = () => {
  const [status, setStatus] = useState<SystemStatus | null>(null);
  // What the device says is in effect (SQMeter spec 020).
  const [effective, setEffective] = useState<EffectiveReport | null>(null);
  const loadStatus = () => {
    void fetchEffectiveReport().then((report) => report && setEffective(report));
    return fetchStatus()
      .then((data) => data && setStatus(data))
      .catch(() => undefined);
  };
  useEffect(() => {
    loadStatus();
    const timer = setInterval(loadStatus, STATUS_REFRESH_MS);
    return () => clearInterval(timer);
  }, []);
  return { status, effective, loadStatus };
};

type Fetched = Awaited<ReturnType<typeof fetchConfigWithRevision>>;

// The settings into the editor; a failure is rendered by the caller as the load-failure state.
const loadInto = (take: (data: Fetched) => void, done: () => void) =>
  fetchConfigWithRevision()
    .then(take)
    .catch(() => undefined)
    .finally(done);

// The page now knows the device as before plus this save. Only when no other
// save came in between is the new revision its own; otherwise it keeps the old
// one, so its next save is checked field by field.
const afterSave = (before: Loaded, patch: unknown, revision?: number): Loaded => ({
  document: applyPatch(before.document, asShown(patch, before.document)),
  revision: before.revision !== undefined && revision === before.revision + 1 ? revision : before.revision,
});

// Spec 021, after a `conflict`: the device's settings, with every edit made here
// put back except those to the fields that changed there (and their credential
// partners), and which edits were replaced. Null when the device couldn't be read.
const rebaseOnto = async (saved: Config, config: Config, changed: string[]) => {
  try {
    const fresh = await fetchConfigWithRevision();
    const { config: next, replaced } = rebaseEdits(toConfigPayload(fresh.config), saved, config, changed);
    return { fresh, next: toConfigPayload(next), replaced };
  } catch {
    return null;
  }
};

// The settings as loaded and as edited, and what the page loaded (spec 021).
const useLoadedConfig = () => {
  const [config, setConfig] = useState<Config | null>(null);
  const [saved, setSaved] = useState<Config | null>(null);
  // The setup state as loaded (spec 015): kept out of the edited settings (a Settings save never sends it).
  const [setup, setSetup] = useState<Config['setup']>(undefined);
  const [loading, setLoading] = useState(true);
  const [validationErrors, setValidationErrors] = useState<ValidationErrors>({});
  const [originalWifiSsid, setOriginalWifiSsid] = useState<string | null>(null);
  const loaded = useRef<Loaded>({ document: null, revision: undefined });
  // `edited`: what the editor shows instead of the device's settings (a rebase).
  const take = ({ config: data, revision }: Fetched, edited?: Config) => {
    const normalized = toConfigPayload(data);
    loaded.current = { document: data, revision };
    setConfig(edited ?? normalized);
    setSaved(normalized);
    setSetup(data.setup);
    setOriginalWifiSsid(data.wifi?.ssid ?? '');
    if (edited) setValidationErrors(getConfigValidationErrors(edited, normalized));
  };
  const load = () => loadInto(take, () => setLoading(false));
  // eslint-disable-next-line react-hooks/exhaustive-deps -- loaded once on mount; reload() loads again
  useEffect(() => void load(), []);
  return {
    config,
    setConfig,
    saved,
    setSaved,
    setup,
    setSetup,
    loading,
    validationErrors,
    setValidationErrors,
    originalWifiSsid,
    loaded,
    take,
    load,
  };
};

export const useConfigEditor = () => {
  const state = useLoadedConfig();
  const { config, setConfig, saved, setSaved, setup, setSetup, loading, validationErrors, setValidationErrors, originalWifiSsid } = state;
  const { loaded, take, load } = state;
  const { status, effective, loadStatus } = useDeviceState();
  const applyChanges = (changes: [ConfigPath, unknown][]) => {
    setConfig((current) => {
      if (!current) return current;
      const next = toConfigPayload(changes.reduce((acc, [path, value]) => setPath(acc, path, value), current));
      setValidationErrors(getConfigValidationErrors(next, saved));
      return next;
    });
  };

  const dirty = useMemo(() => Boolean(config && saved && JSON.stringify(config) !== JSON.stringify(saved)), [config, saved]);

  // Saves the settings as edited, with `extra` (the wizard's setup state) added.
  const save = async (extra: Partial<Config> = {}): Promise<SaveOutcome> => {
    if (!config || !saved) return { kind: 'unreachable' };
    const before = loaded.current;
    const { outcome, patch, revision } = await submit(config, saved, extra, before);
    setValidationErrors(outcome.kind === 'invalid' ? outcome.errors : {});
    if (outcome.kind !== 'saved') return outcome;
    loaded.current = afterSave(before, patch, revision);
    setSaved(outcome.payload);
    setConfig(outcome.payload);
    if (extra.setup) setSetup(extra.setup);
    loadStatus();
    return outcome;
  };

  const discard = () => {
    setConfig(saved);
    setValidationErrors({});
  };

  // Spec 021: after a `conflict`, the fields whose edits were replaced (null: unreachable).
  const rebase = async (changed: string[]): Promise<string[] | null> => {
    const rebased = config && saved ? await rebaseOnto(saved, config, changed) : null;
    if (rebased) take(rebased.fresh, rebased.next);
    return rebased?.replaced ?? null;
  };

  return {
    config,
    saved,
    setup,
    status,
    effective,
    loading,
    validationErrors,
    originalWifiSsid,
    dirty,
    applyChanges,
    save,
    discard,
    rebase,
    loadStatus,
    reload: () => void load(), // spec 019: after the device saved a taught proposal itself
  };
};

export type ConfigEditor = ReturnType<typeof useConfigEditor>;
