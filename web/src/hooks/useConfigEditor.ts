import { useEffect, useMemo, useState } from 'preact/hooks';
import type { Config, SystemStatus } from '../types';
import { getConfigValidationErrors, hasConfigValidationErrors, type ValidationErrors } from '../validation/configSchema';
import type { ConfigPath } from '../components/settings/context';
import { toConfigPayload } from '../components/settings/payload';
import { restartReasons } from '../components/settings/restart';
import { fetchEffectiveReport, type EffectiveReport } from '../lib/settingsDeps';
import { fetchConfig, fetchStatus, saveConfig } from '../lib/settingsApi';
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
  | { kind: 'unreachable' };

// Validates `config` (with `extra` added to what is sent) and saves it.
const submit = async (config: Config, saved: Config, extra: Partial<Config>): Promise<SaveOutcome> => {
  const payload = toConfigPayload(config);
  const errors = getConfigValidationErrors({ ...payload, ...extra });
  if (hasConfigValidationErrors(errors)) return { kind: 'invalid', errors };
  try {
    const { ok, body } = await saveConfig({ ...payload, ...extra });
    if (!ok || body?.success === false) return { kind: 'refused', message: deviceError(body, t('settings.failedToSaveSettings')) };
    return { kind: 'saved', payload, restart: restartReasons(saved, payload) };
  } catch {
    return { kind: 'unreachable' };
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

export const useConfigEditor = () => {
  const [config, setConfig] = useState<Config | null>(null);
  const [saved, setSaved] = useState<Config | null>(null);
  // The setup state as loaded (spec 015): kept out of the edited settings, so
  // a Settings save never sends it.
  const [setup, setSetup] = useState<Config['setup']>(undefined);
  const [loading, setLoading] = useState(true);
  const [validationErrors, setValidationErrors] = useState<ValidationErrors>({});
  const [originalWifiSsid, setOriginalWifiSsid] = useState<string | null>(null);
  const { status, effective, loadStatus } = useDeviceState();

  useEffect(() => {
    fetchConfig()
      .then((data) => {
        const normalized = toConfigPayload(data);
        setConfig(normalized);
        setSaved(normalized);
        setSetup(data.setup);
        setOriginalWifiSsid(data.wifi?.ssid ?? '');
      })
      .catch(() => undefined) // rendered by the caller as the load-failure state
      .finally(() => setLoading(false));
  }, []);

  const applyChanges = (changes: [ConfigPath, unknown][]) => {
    setConfig((current) => {
      if (!current) return current;
      const next = toConfigPayload(changes.reduce((acc, [path, value]) => setPath(acc, path, value), current));
      setValidationErrors(getConfigValidationErrors(next));
      return next;
    });
  };

  const dirty = useMemo(() => Boolean(config && saved && JSON.stringify(config) !== JSON.stringify(saved)), [config, saved]);

  // Saves the settings as edited, with `extra` (the wizard's setup state) added.
  const save = async (extra: Partial<Config> = {}): Promise<SaveOutcome> => {
    if (!config || !saved) return { kind: 'unreachable' };
    const outcome = await submit(config, saved, extra);
    setValidationErrors(outcome.kind === 'invalid' ? outcome.errors : {});
    if (outcome.kind !== 'saved') return outcome;
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
    loadStatus,
  };
};

export type ConfigEditor = ReturnType<typeof useConfigEditor>;
