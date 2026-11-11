import type { MutableRef } from 'preact/hooks';
import type { AlertEventKey, AlertsConfig, Config } from '../../types';
import type { DepEntry } from '../../lib/settingsDeps';
import type { AlertTestResult, useAlertTest } from '../../hooks/useAlertTest';
import type { ConfigPath } from './context';
import { t } from '../../i18n';

// What the Settings > Alerts cards share: the merged alerts settings and the
// tab's helpers (AlertsTab.tsx builds it once per render).
export interface AlertsView {
  config: Config;
  alerts: AlertsConfig;
  off: boolean;
  dirty: boolean;
  pushoverOn: boolean;
  channelCount: number;
  set: (path: string[], value: unknown) => void;
  updateMany: (changes: [ConfigPath, unknown][]) => void;
  err: (key: string) => string | undefined;
  dep: (setting: string) => DepEntry;
  fix: (entry: DepEntry) => void;
  tester: ReturnType<typeof useAlertTest>;
  // Where a clicked {variable} goes: the last focused title/message field.
  lastField: MutableRef<{ key: AlertEventKey; field: 'title' | 'message'; el: HTMLInputElement | HTMLTextAreaElement } | null>;
}

export const isPending = (result: AlertTestResult | null) => result?.type === 'pending';

export const LEVEL_OPTIONS = [
  { value: '0', label: t('settings.alerts.off') },
  { value: '1', label: t('settings.alerts.quiet') },
  { value: '2', label: t('settings.alerts.normal') },
  { value: '3', label: t('settings.alerts.urgent') },
  { value: '4', label: t('settings.alerts.wakeMe') },
];

export const LEVEL_HINT = t('settings.alerts.quietNoSoundUrgentBreaks');

// Pushover's built-in sounds; a custom one already saved stays selectable.
const PUSHOVER_SOUNDS = [
  'pushover',
  'bike',
  'bugle',
  'cashregister',
  'classical',
  'cosmic',
  'falling',
  'gamelan',
  'incoming',
  'intermission',
  'magic',
  'mechanical',
  'pianobar',
  'siren',
  'spacealarm',
  'tugboat',
  'alien',
  'climb',
  'persistent',
  'echo',
  'updown',
  'vibrate',
  'none',
];
const LONG_SOUNDS = new Set(['alien', 'climb', 'persistent', 'echo', 'updown']);
const soundLabel = (sound: string) =>
  sound === 'none'
    ? t('settings.alerts.silent')
    : sound === 'vibrate'
      ? t('settings.alerts.vibrateOnly')
      : `${sound[0].toUpperCase()}${sound.slice(1)}${LONG_SOUNDS.has(sound) ? ' (long)' : ''}`;
export const soundOptions = (current: string, defaultLabel: string) => [
  { value: '', label: defaultLabel },
  ...(current && !PUSHOVER_SOUNDS.includes(current) ? [current] : [])
    .concat(PUSHOVER_SOUNDS)
    .map((sound) => ({ value: sound, label: soundLabel(sound) })),
];

// The firmware's built-in wording (lib/AlertLogic), written as templates;
// shown as the placeholder until you write your own.
const DEFAULT_TEXT: Record<AlertEventKey, { title: string; message: string }> = {
  client_lost: {
    title: t('settings.alerts.imagingAppStoppedChecking'),
    message: t('settings.alerts.noRequestToTheDevice'),
  },
  client_back: { title: t('settings.alerts.imagingAppIsBack'), message: t('settings.alerts.theDeviceIsBeingChecked') },
  client_disconnected: { title: t('settings.alerts.imagingAppDisconnected'), message: t('settings.alerts.theDeviceWasDisconnected') },
};

export const defaultText = (key: AlertEventKey) => DEFAULT_TEXT[key];

const VAR_HELP: Record<string, string> = {
  event: t('settings.alerts.varEvent'),
  device: t('settings.alerts.deviceName'),
  time: t('settings.alerts.localTime'),
  date: t('settings.alerts.localDate'),
  level: t('settings.alerts.varLevel'),
  silent_for: t('settings.alerts.theSilentForTimeE'),
  last_checked: t('settings.alerts.whenTheImagingAppLast'),
  client_id: t('settings.alerts.theAlpacaClientidTheImaging'),
};
// For the imaging-app events {device} is the Alpaca device, not the device name.
const CLIENT_VAR_HELP: Record<string, string> = { device: t('settings.alerts.theAlpacaDevice') };
const CLIENT_EVENTS: AlertEventKey[] = ['client_lost', 'client_back', 'client_disconnected'];

export const varHelp = (key: AlertEventKey, name: string) =>
  (CLIENT_EVENTS.includes(key) ? CLIENT_VAR_HELP[name] : undefined) ?? VAR_HELP[name];

// On this tab the "Alerts are off" link (D-04) is shown once, by the Send
// alerts switch, not on every row - and channels can be set up and tested
// while alerts are off.
export const withoutAlertsOff = (entry: DepEntry): DepEntry => {
  const { blockedBy, ...rest } = entry;
  const next: DepEntry = blockedBy && blockedBy.reason !== 'alerts-off' ? { ...rest, blockedBy } : rest;
  return entry.reason === 'alerts-off'
    ? { ...next, state: 'active', reason: undefined, text: undefined, fix: undefined, id: entry.id }
    : next;
};
