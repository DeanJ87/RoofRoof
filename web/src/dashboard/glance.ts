import type { AlertSchedule, Config, RoofClosePhase, SyncNeed, SystemStatus } from '../types';
import { needs } from '../lib/syncView';
import { REASONS, type DepEntry, type EffectiveReport } from '../lib/settingsDeps';
import { describeSchedule } from '../components/settings/alertSchedule';
import { describeClient, type ClientView } from '../lib/alpacaClients';
import { t } from '../i18n';
import { appHref } from '../lib/appHref';
import { deviceText } from '../i18n/deviceMessage';
import { closePhasePill } from '../lib/roofView';
import { setupStages } from '../lib/setupProgress';

// What the Status card shows (SQMeter specs 025 content, 026 presentation).
// The card's job is the imaging app and whether alerts go out, plus problems
// no other card shows (DS-08). One function decides what shows and in which
// order; StatusCard only renders it. Each item's id is an entry of
// inventory.json. Every item is short: a name (`label`), a one- or two-word
// state for its pill, and the detail behind "?".

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
  schedule: AlertSchedule | null; // live alert schedule (useAlertSchedule)
}

const PRIORITY: Record<string, number> = {
  'alerts-state': 3,
  'alerts-mode-not-in-effect': 3,
  'no-channel': 3,
  'roof-fault': 2,
  'roof-close': 2,
  'imaging-app': 4,
  'clock-location': 7,
  'sqmeter-needs': 8,
  'test-box': 2,
  setup: 9,
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
// each Alpaca device? One tile per device; none while there is no Alpaca
// device, including when Alpaca is off (FR-016).
const imagingAppItems = ({ config, status }: GlanceInput): GlanceItem[] => {
  const clients = status?.alpaca?.clients;
  // The device lists its Dome's client state even with Alpaca off: no tile then.
  if (!clients || status?.alpaca?.enabled === false) return [];
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

// The roof's problems (spec 009 FR-015, FR-016; DS-07): a latched fault, and an
// automatic close held by a person, unable to move or halted. The state itself is
// the Roof card's (DS-08).
const roofFaultItem = ({ status }: GlanceInput): GlanceItem | null => {
  const fault = status?.roof?.fault;
  return fault
    ? item('roof-fault', 'problem', { label: t('roof.title'), state: t('roof.state.fault'), detail: deviceText(fault.text) })
    : null;
};

const STUCK: Partial<Record<RoofClosePhase, Severity>> = { heldByPerson: 'note', cannotMove: 'problem', halted: 'problem' };

const roofCloseItem = ({ status }: GlanceInput): GlanceItem | null => {
  const phase = status?.roof?.closePhase;
  const severity = phase ? STUCK[phase] : undefined;
  const pill = phase ? closePhasePill(phase) : null;
  if (!severity || !pill) return null;
  return item('roof-close', severity, { label: t('roof.closePhase.label'), state: pill.label, detail: pill.text });
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

// Spec 020: a secret SQMeter has and RoofRoof lacks (after a synced broker change, say). A note, never a problem.
const NEED_TAB: Record<SyncNeed, string> = { mqttPassword: 'network', ntfyToken: 'alerts', pushoverKeys: 'alerts' };
const needItems = ({ status }: GlanceInput): GlanceItem[] =>
  (['mqttPassword', 'ntfyToken', 'pushoverKeys'] as const)
    .filter((need) => needs(status?.sync, need))
    .map((need) =>
      item('sqmeter-needs', 'note', {
        label: t(`glance.needs.${need}`),
        state: t('glance.needs.state'),
        detail: t(`sync.needs.${need}`),
        fix: settingsLink(NEED_TAB[need]),
      }),
    );

// Spec 022: while a test box bypass is in effect the card never says "All good"
// (constitution 4.1.0: a bypassed check is never shown as passing).
const testBoxItem = ({ status }: GlanceInput): GlanceItem | null =>
  status?.roof?.testBox?.active
    ? item('test-box', 'note', {
        label: t('testBox.title'),
        state: t('testBox.check.bypassed'),
        detail: t('status.testBoxHint'),
        fix: { label: t('testBox.banner.manage'), href: appHref('/test-box') },
      })
    : null;

// Spec 028: an unfinished roof setup is one line, "Finish setup: <first stage left>",
// linking to that stage; the full list is in Settings → Roof. A stage the test box
// bypasses isn't counted here: the test box row says so.
const setupItem = ({ config }: GlanceInput): GlanceItem | null => {
  const left = setupStages(config).filter((stage) => stage.state === 'todo' || stage.state === 'typed');
  const first = left[0];
  return first
    ? item('setup', 'note', {
        label: t('stage.finish', { stage: first.title }),
        state: t('stage.left', { count: left.length }),
        detail: first.note,
        fix: { label: first.title, href: appHref(first.href) },
      })
    : null;
};

/** Everything the Status card shows, most important first. */
export const glanceItems = (input: GlanceInput): GlanceItem[] => {
  const items = [
    alertsItem(input),
    sendModeItem(input),
    channelsItem(input),
    roofFaultItem(input),
    roofCloseItem(input),
    testBoxItem(input),
    ...imagingAppItems(input),
    clockItem(input),
    ...needItems(input),
    setupItem(input),
  ].filter((entry): entry is GlanceItem => Boolean(entry));
  const rank = { problem: 0, note: 1, ok: 2, idle: 3 } as const;
  return items.sort((a, b) => a.priority - b.priority || rank[a.severity] - rank[b.severity]);
};

/** How many things need a look: every item that isn't ok. */
export const toCheck = (items: GlanceItem[]) => items.filter((entry) => entry.severity === 'note' || entry.severity === 'problem');

/** A screen-reader line for an item, e.g. "Alerts: Paused". */
export const spoken = (entry: GlanceItem) => `${entry.label}: ${entry.state}`;
