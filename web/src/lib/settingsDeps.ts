import type { AlertEventKey, Config } from '../types';
import { t } from '../i18n';
import { deviceText } from '../i18n/deviceMessage';
import { REASONS } from './settingsDepReasons';
import { blocker, decide, on, read, type Link, type Shown } from './settingsDepLinks';

// Whether each setting that needs another setting (or hardware, or the
// network) is in effect - the device's rules from lib/SettingsDeps, mirrored
// so Settings can preview unsaved changes. The device's own report
// (GET /api/settings/effective) wins whenever the form is clean;
// test/fixtures/settings-deps/cases.json holds both to the same answers
// (SQMeter spec 020).

export type DepState = 'off' | 'active' | 'inactive' | 'unknown';
export type Unmet = 'inactive' | 'fail-safe';

export interface DepFacts {
  wifiConnected: boolean;
  mqttConnected: boolean;
  clockSet: boolean;
  alpacaDevices: number; // Alpaca devices offered (spec 006 adds the Dome)
}

export interface DepEntry {
  id: string;
  setting: string;
  state: DepState;
  reason?: string;
  text?: string;
  fix?: string; // "tab#anchor" or "restart"
  unmet?: Unmet;
  neutral?: boolean;
  // While the setting is off: what would make it inactive if it were
  // switched on (local evaluation only; the device doesn't report it).
  blockedBy?: { id: string; reason: string; text: string; fix: string };
}

export interface EffectiveReport {
  facts: DepFacts;
  settings: DepEntry[];
}

// The reasons' texts and fix targets (same as lib/SettingsDeps/catalogue.json) and fix labels.
export { FIX_LABEL, REASONS } from './settingsDepReasons';

const EVENT_KEYS: AlertEventKey[] = ['client_lost', 'client_back', 'client_disconnected'];
// The roof's non-critical events need alerts on; the critical two don't (spec 007).
const ROOF_EVENT_KEYS: AlertEventKey[] = ['move_fault', 'supply_low', 'unsafe_closing', 'roof_closed', 'safety_lost'];

// What every section needs, worked out once.
interface Context {
  config: Config;
  fact: <K extends keyof DepFacts>(key: K) => DepFacts[K] | null;
  add: (setting: string, id: string, isOn: boolean, chain: Link[], shown?: Shown) => void;
}

const level = (c: Context, key: AlertEventKey) => read(c.config, `alerts.events.${key}.level`, 0);

// Alert channels are push only: they need the master switch. Links in
// catalogue order, so the channel's own dependency comes before "alerts are
// off" - a test send ignores the latter.
const addAlertChannels = (c: Context) => {
  const alertsOn: Link = ['D-04', 'alerts-off', on(c.config, 'alerts.enabled')];
  const wifi: Link = ['D-03', 'wifi-disconnected', c.fact('wifiConnected')];
  for (const channel of ['pushover', 'ntfy', 'webhook'])
    c.add(`alerts.${channel}.enabled`, 'D-03', on(c.config, `alerts.${channel}.enabled`), [wifi, alertsOn]);
  c.add('alerts.mqtt.enabled', 'D-01', on(c.config, 'alerts.mqtt.enabled'), [
    ['D-01', 'mqtt-off', on(c.config, 'mqtt.enabled')],
    ['D-02', 'mqtt-disconnected', c.fact('mqttConnected')],
    alertsOn,
  ]);
};

// Imaging apps connect over Alpaca (SQMeter spec 021), to a device it offers.
const addAlertEvents = (c: Context) => {
  const alertsOn: Link = ['D-04', 'alerts-off', on(c.config, 'alerts.enabled')];
  const alpacaOn: Link = ['D-37', 'alpaca-off', on(c.config, 'alpaca.enabled')];
  const devices = c.fact('alpacaDevices');
  const device: Link = ['D-37', 'no-alpaca-device', devices === null ? null : devices > 0];
  for (const key of EVENT_KEYS) c.add(`alerts.events.${key}.level`, 'D-37', level(c, key) !== 0, [alertsOn, alpacaOn, device]);
  for (const key of ROOF_EVENT_KEYS) c.add(`alerts.events.${key}.level`, 'D-04', level(c, key) !== 0, [alertsOn]);
};

const addAlertOptions = (c: Context) => {
  c.add('alerts.sendMode', 'D-12', read<string>(c.config, 'alerts.sendMode', 'any') === 'whileConnected', [
    ['D-04', 'alerts-off', on(c.config, 'alerts.enabled')],
    ['D-12', 'alpaca-off', on(c.config, 'alpaca.enabled')],
  ]);
};

const addMqtt = (c: Context) => {
  const mqttOn: Link = ['D-13', 'mqtt-off', on(c.config, 'mqtt.enabled')];
  const homeAssistant = on(c.config, 'mqtt.homeAssistant.enabled');
  c.add('mqtt.homeAssistant.enabled', 'D-13', homeAssistant, [mqttOn]);
  c.add('mqtt.homeAssistant.alertsSwitch', 'D-13', homeAssistant, [mqttOn, ['D-13', 'alerts-off', on(c.config, 'alerts.enabled')]]);
};

// The SQMeter MQTT safety source is read from RoofRoof's own broker (spec 004).
const addSafety = (c: Context) => {
  c.add('safety.mqtt.enabled', 'D-40', on(c.config, 'safety.mqtt.enabled'), [
    ['D-40', 'mqtt-off', on(c.config, 'mqtt.enabled')],
    ['D-41', 'mqtt-disconnected', c.fact('mqttConnected')],
  ]);
};

const addDevice = (c: Context) => {
  const wifi: Link = ['D-28', 'wifi-disconnected', c.fact('wifiConnected')];
  c.add('ntp.enabled', 'D-28', on(c.config, 'ntp.enabled'), [wifi]);
  c.add('ota.enabled', 'D-32', on(c.config, 'ota.enabled'), [['D-32', 'ota-no-password', read(c.config, 'ota.password', '') !== '']]);
  c.add('wifi.mdns', 'D-36', on(c.config, 'wifi.mdns', true), [['D-36', 'wifi-disconnected', c.fact('wifiConnected')]]);
  c.add('wifi.ipv6', 'D-36', on(c.config, 'wifi.ipv6', true), [['D-36', 'wifi-disconnected', c.fact('wifiConnected')]]);
};

/**
 * Every reported setting, in catalogue order. With `facts === null` (status
 * not loaded yet) runtime and hardware links are unknown: they never make a
 * setting inactive, and a setting only they could decide is "unknown".
 */
export function evaluate(config: Config, facts: DepFacts | null): DepEntry[] {
  const out: DepEntry[] = [];
  const fact = <K extends keyof DepFacts>(key: K): DepFacts[K] | null => (facts ? facts[key] : null);
  const context: Context = {
    config,
    fact,
    add: (setting, id, isOn, chain, shown = {}) => {
      const entry: DepEntry = { id, setting, state: 'off', ...shown };
      if (isOn) decide(entry, chain);
      else entry.blockedBy = blocker(chain);
      if (!entry.blockedBy) delete entry.blockedBy;
      out.push(entry);
    },
  };
  [addAlertChannels, addAlertEvents, addAlertOptions, addMqtt, addSafety, addDevice].forEach((section) => section(context));
  return out;
}

/** Lookup by setting path; what Settings shows for each dependent setting. */
export interface EffectiveView {
  get: (setting: string) => DepEntry;
  /** Inactive with a reason, or null. */
  reason: (setting: string) => DepEntry | null;
}

const UNKNOWN = (setting: string): DepEntry => ({ id: '', setting, state: 'unknown' });

export const viewOf = (entries: DepEntry[] | null): EffectiveView => {
  const bySetting = new Map((entries ?? []).map((e) => [e.setting, e]));
  const get = (setting: string) => bySetting.get(setting) ?? UNKNOWN(setting);
  return {
    get,
    reason: (setting) => {
      const entry = get(setting);
      return entry.state === 'inactive' ? entry : null;
    },
  };
};

// The device reports its reasons in English; show the UI's own wording for
// the same reason (the device's text, translated, for a reason this UI
// doesn't know).
const inLanguage = (entry: DepEntry): DepEntry => {
  const known = entry.reason ? REASONS[entry.reason] : undefined;
  return entry.text === undefined ? entry : { ...entry, text: known?.text ?? deviceText(entry.text) };
};

/**
 * What Settings shows: the device's report while the form matches what the
 * device runs, else a preview of the draft with the device's facts (FR-006).
 */
export const effectiveEntries = (draft: Config, report: EffectiveReport | null, dirty: boolean): DepEntry[] => {
  const local = evaluate(draft, report?.facts ?? null);
  if (!report || dirty) return local;
  const device = new Map(report.settings.map((e) => [e.setting, e]));
  // Switched on in the form: the device's answer. Off: the local entry,
  // which also says what would block switching it on.
  return local.map((e) => {
    const reported = device.get(e.setting);
    return e.state !== 'off' && reported && reported.state !== 'off' ? inLanguage(reported) : e;
  });
};

// Runtime conditions (network, clock) never block a toggle; settings do
// (SQMeter spec 020 FR-005). The OTA password is typed in once the setting is
// on, so it doesn't block it either; nor does a missing Alpaca device, which
// arrives with a firmware update.
const NON_BLOCKING = new Set(['wifi-disconnected', 'mqtt-disconnected', 'clock-unset', 'ota-no-password', 'no-alpaca-device']);
export const blocksSwitchingOn = (reason: string | undefined) => reason !== undefined && !NON_BLOCKING.has(reason);

/** Settings that saving the draft would make inactive (were active or off before). */
export const newlyInactive = (saved: DepEntry[], draft: DepEntry[]): DepEntry[] => {
  const before = new Map(saved.map((e) => [e.setting, e.state]));
  return draft.filter((e) => e.state === 'inactive' && !e.neutral && before.get(e.setting) !== 'inactive');
};

/** The note under a dependent setting: why it's inactive, or what keeps it from being switched on. */
export interface DepNoteContent {
  id: string;
  reason: string;
  text: string;
  tone: 'warn' | 'info';
  target: DepEntry; // what its fix action goes to
}

export const noteFor = (entry: DepEntry, prefix = t('settingsDeps.inactive')): DepNoteContent | null => {
  if (entry.state === 'inactive' && entry.reason) {
    return {
      id: entry.id,
      reason: entry.reason,
      text: t('settingsDeps.inactiveBecause', { prefix, reason: entry.text }),
      tone: entry.neutral ? 'info' : 'warn',
      target: entry,
    };
  }
  const blocked = entry.state === 'off' ? entry.blockedBy : undefined;
  return blocked
    ? { id: blocked.id, reason: blocked.reason, text: `${blocked.text}.`, tone: 'info', target: { ...entry, ...blocked } }
    : null;
};

/** What saving the draft would make inactive; nothing while the form is clean (FR-006). */
export const previewInactive = (draft: Config, saved: Config | null, report: EffectiveReport | null, dirty: boolean): DepEntry[] =>
  dirty && saved ? newlyInactive(effectiveEntries(saved, report, false), evaluate(draft, report?.facts ?? null)) : [];

/** Where an inactive setting's one-click fix goes: a Settings tab and section, or a restart. */
export const fixTarget = (entry: DepEntry): { restart: true } | { tab: string; anchor?: string } => {
  if (entry.fix === 'restart') return { restart: true };
  const [tab, anchor] = (entry.fix ?? '').split('#');
  return { tab, anchor };
};

/** GET /api/settings/effective: the device's report, or null if it can't be read. */
export const fetchEffectiveReport = async (): Promise<EffectiveReport | null> => {
  try {
    const response = await fetch('/api/settings/effective');
    const data = response.ok ? await response.json() : null;
    return data && Array.isArray(data.settings) ? (data as EffectiveReport) : null;
  } catch {
    return null; // device unreachable: Settings previews from the form alone
  }
};
