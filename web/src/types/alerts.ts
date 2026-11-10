// 0 off, 1 quiet, 2 normal, 3 urgent, 4 wake me
export type AlertLevel = 0 | 1 | 2 | 3 | 4;

// The imaging-app events (SQMeter spec 021). Spec 007 adds the roof's.
export type AlertEventKey = 'client_lost' | 'client_back' | 'client_disconnected';

// sound: Pushover sound name; empty uses the Pushover default.
export interface AlertEventSetting {
  level: AlertLevel;
  sound: string;
  // Custom wording with {variables}; empty or missing uses the default.
  title?: string;
  message?: string;
}

export interface AlertsConfig {
  enabled: boolean;
  events: Record<AlertEventKey, AlertEventSetting>;
  // When alerts are sent (SQMeter spec 021).
  sendMode: AlertSendMode;
  clientSilentSeconds: number; // 30..3600
  cooldownSeconds: number;
  pushover: { enabled: boolean; userKey: string; appToken: string; sound: string };
  ntfy: { enabled: boolean; server: string; topic: string; token: string };
  webhook: { enabled: boolean; url: string; authHeader: string; insecureTls: boolean };
  mqtt: { enabled: boolean };
}

export type AlertChannelName = 'mqtt' | 'pushover' | 'ntfy' | 'webhook';

export type AlertSendMode = 'any' | 'whileConnected';

export type AlertScheduleReason =
  'none' | 'user-ui' | 'user-rest' | 'user-mqtt' | 'client-connected' | 'client-disconnected' | 'waiting-for-client' | 'migrated';

// GET /api/alerts/armed, and /api/status "alerts".
export interface AlertSchedule {
  armed: boolean;
  mode: AlertSendMode;
  reason: AlertScheduleReason;
  since?: string | null; // ISO 8601 UTC, null without a clock
  sinceAgeMs?: number | null; // null: before this boot
  recentRevision?: number; // /api/status only: changes whenever /api/alerts/recent would
}

// One Alpaca device as the imaging app sees it (/api/status "alpaca").
export interface AlpacaClientState {
  connected: boolean;
  watching: boolean;
  silent: boolean;
  lastCheckedAgeMs: number | null;
  clientId: number | null;
}

export interface AlertRecord {
  id: number;
  event: string;
  title: string;
  message: string;
  level: 'quiet' | 'normal' | 'urgent' | 'wake' | 'off';
  ageSeconds: number;
  timestamp?: number;
  channels: Partial<Record<AlertChannelName, { status: 'pending' | 'sent' | 'failed' | 'skipped'; detail: string }>>;
}

export interface AlertsRecent {
  enabled: boolean;
  armed: boolean; // sending, or paused
  alerts: AlertRecord[];
}
