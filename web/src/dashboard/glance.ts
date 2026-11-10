import type { AlertSchedule, Config, SystemStatus } from '../types';
import { REASONS, type DepEntry, type EffectiveReport } from '../lib/settingsDeps';
import { describeSchedule } from '../components/settings/alertSchedule';
import { describeClient, type ClientView } from '../lib/alpacaClients';
import { t } from '../i18n';
import { appHref } from '../lib/appHref';

// What the Status card shows (SQMeter specs 025 content, 026 presentation).
// The card's job is the imaging app and whether alerts go out, plus problems
// no other card shows (DS-08). One function decides what shows and in which
// order; StatusCard only renders it. Each item's id is an entry of
// inventory.json. Every item is short: a name (`label`), a one- or two-word
// state for its pill, and the detail behind "?". Spec 009 adds the roof.

// idle: a normal resting state that needs nothing (no imaging app connected while
// alerts don't depend on one): shown dim, never counted as something to check.
export type Severity = 'ok' | 'idle' | 'note' | 'problem';

export type GlanceFix = { label: string; href: string } | { label: string; action: 'resume' };

export interface GlanceItem {
  id: string; // inventory.json entry
  severity: Severity;
  priority: number; // the Edge Cases order
  label: string; // the tile label or row name, e.g. "Alerts"
  state: string; // the pill, e.g. "Paused"
  sub?: string; // which one, for tiles that repeat (the imaging app per Alpaca device)
  checked?: string; // when that one last checked in, its own line (DS-24)
  detail?: string; // behind "?": the consequence or the list behind a summary
  fix?: GlanceFix;
}

export interface GlanceInput {
  status: SystemStatus | null;
  config: Config | null;
  effective: EffectiveReport | null;
  connected: boolean; // the stream is open
  quiet: boolean; // no message for longer than the quiet threshold
  schedule: AlertSchedule | null; // live alert schedule (useAlertSchedule)
}

const PRIORITY: Record<string, number> = {
  freshness: 1,
  'alerts-state': 3,
  'alerts-mode-not-in-effect': 3,
  'no-channel': 3,
  'imaging-app': 4,
  'clock-location': 7,
};

interface Parts {
  label: string;
  state: string;
  sub?: string;
  checked?: string;
  detail?: string;
  fix?: GlanceFix;
}

const item = (id: string, severity: Severity, parts: Parts): GlanceItem => ({ id, severity, priority: PRIORITY[id], ...parts });

// Alert channels (SQMeter spec 020) whose being inactive means alerts can't go out.
const ALERT_CHANNEL_DEPS = new Set(['D-01', 'D-02', 'D-03', 'D-04']);

const settingsLink = (tab: string, anchor?: string): GlanceFix => ({
  label: t('glance.openSettings'),
  href: appHref(`/settings?tab=${tab}${anchor ? `&section=${anchor}` : ''}`),
});

// The live connection to the device, when it isn't live.
const freshness = ({ connected, quiet }: GlanceInput): GlanceItem | null => {
  const label = t('status.data');
  if (!connected) return item('freshness', 'problem', { label, state: t('status.offline'), detail: t('status.offlineHint') });
  if (quiet) return item('freshness', 'problem', { label, state: t('status.noUpdates'), detail: t('status.noUpdatesHint') });
  return null;
};

const alertsItem = ({ config, schedule }: GlanceInput): GlanceItem | null => {
  if (!config?.alerts) return null;
  const label = t('status.alerts');
  if (!config.alerts.enabled) return item('alerts-state', 'note', { label, state: t('status.off'), detail: t('status.nothingSent') });
  if (!schedule) return null;
  if (schedule.armed) return item('alerts-state', 'ok', { label, state: t('status.sending') });
  const waiting = schedule.reason === 'waiting-for-client' || schedule.reason === 'client-disconnected';
  return waiting
    ? item('alerts-state', 'problem', { label, state: t('status.waiting'), detail: t('glance.alertsWaitingDetail') })
    : item('alerts-state', 'problem', {
        label,
        state: t('status.paused'),
        detail: describeSchedule(schedule),
        fix: { label: t('alertsBell.resume'), action: 'resume' },
      });
};

// The device reports its reason in English; Settings shows the translated text.
const reasonText = (entry: DepEntry) => (entry.reason && REASONS[entry.reason]?.text) || entry.text || '';

const inactive = (effective: EffectiveReport | null, ids: Set<string>): DepEntry[] =>
  (effective?.settings ?? []).filter((entry) => entry.state === 'inactive' && ids.has(entry.id));

const sendModeItem = ({ config, effective, status }: GlanceInput): GlanceItem | null => {
  if (config?.alerts?.sendMode !== 'whileConnected') return null;
  const alpacaOff = status?.alpaca ? !status.alpaca.enabled : effective?.settings.some((e) => e.id === 'D-12' && e.state === 'inactive');
  if (!alpacaOff) return null;
  return item('alerts-mode-not-in-effect', 'note', {
    label: t('status.sendMode'),
    state: t('status.notInEffect'),
    detail: t('status.sendModeHint'),
    fix: settingsLink('network', 'alpaca'),
  });
};

const channelsItem = ({ config, effective }: GlanceInput): GlanceItem | null => {
  const alerts = config?.alerts;
  if (!alerts?.enabled) return null;
  const label = t('status.channels');
  const on = (['pushover', 'ntfy', 'webhook', 'mqtt'] as const).filter((channel) => alerts[channel]?.enabled);
  if (!on.length)
    return item('no-channel', 'problem', { label, state: t('status.noneOn'), detail: t('status.noneOnHint'), fix: settingsLink('alerts') });
  const blocked = inactive(effective, ALERT_CHANNEL_DEPS);
  const working = on.filter((channel) => !blocked.some((entry) => entry.setting === `alerts.${channel}.enabled`));
  if (working.length || !blocked.length) return null;
  return item('no-channel', 'problem', { label, state: t('status.cantSend'), detail: reasonText(blocked[0]), fix: settingsLink('alerts') });
};

const SEVERITY: Record<ClientView['tone'], Severity> = { ok: 'ok', warn: 'problem', muted: 'note' };

// The card's main job: is an imaging app (N.I.N.A. and the like) watching
// each Alpaca device? One tile per device; one tile when Alpaca is off.
const imagingAppItems = ({ config, status }: GlanceInput): GlanceItem[] => {
  if (status?.alpaca?.enabled === false)
    return [
      item('imaging-app', 'idle', {
        label: t('status.imagingApp'),
        state: t('status.alpacaOff'),
        detail: t('status.alpacaOffHint'),
        fix: settingsLink('network', 'alpaca'),
      }),
    ];
  const clients = status?.alpaca?.clients;
  if (!clients) return [];
  // Not connected only needs a look when alerts wait for an imaging app.
  const needed = config?.alerts?.enabled && config.alerts.sendMode === 'whileConnected';
  return Object.entries(clients).map(([device, state]) => {
    const view = describeClient(state);
    const severity = view.tone === 'muted' && !needed ? 'idle' : SEVERITY[view.tone];
    return item('imaging-app', severity, {
      label: t('status.imagingApp'),
      state: view.state,
      sub: device,
      checked: view.checked,
      detail: state.silent ? t('status.goneQuietHint') : undefined,
    });
  });
};

// The device's clock (NTP): alert times and the imaging-app "last checked" need it.
const clockItem = ({ status }: GlanceInput): GlanceItem | null =>
  status && status.time.epoch === 0
    ? item('clock-location', 'problem', {
        label: t('status.clock'),
        state: t('status.notSet'),
        detail: t('status.clockHint'),
        fix: settingsLink('time'),
      })
    : null;

/** Everything the Status card shows, most important first. */
export const glanceItems = (input: GlanceInput): GlanceItem[] => {
  const items = [freshness(input), alertsItem(input), sendModeItem(input), channelsItem(input), ...imagingAppItems(input), clockItem(input)].filter(
    (entry): entry is GlanceItem => Boolean(entry),
  );
  const rank = { problem: 0, note: 1, ok: 2, idle: 3 } as const;
  return items.sort((a, b) => a.priority - b.priority || rank[a.severity] - rank[b.severity]);
};

/** How many things need a look: every item that isn't ok. */
export const toCheck = (items: GlanceItem[]) => items.filter((entry) => entry.severity === 'note' || entry.severity === 'problem');

/** A screen-reader line for an item, e.g. "Alerts: Paused". */
export const spoken = (entry: GlanceItem) => `${entry.label}: ${entry.state}`;
