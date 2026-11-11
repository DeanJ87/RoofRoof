import type { AlertsConfig, AlpacaConfig, Config } from '../../types';

// Defaults for every settings section, used to fill in fields a config
// doesn't carry (the same values as Config::createDefault).

export const defaultAuthConfig: NonNullable<Config['auth']> = {
  enabled: false,
  username: 'admin',
  password: '',
};

export const defaultAlpacaConfig: AlpacaConfig = {
  enabled: false,
};

export const defaultAlertsConfig: AlertsConfig = {
  enabled: false,
  events: {
    mount_not_parked: { level: 4, sound: '' },
    roof_not_closed: { level: 4, sound: '' },
    move_fault: { level: 3, sound: '' },
    supply_low: { level: 2, sound: '' },
    unsafe_closing: { level: 2, sound: '' },
    roof_closed: { level: 2, sound: '' },
    safety_lost: { level: 2, sound: '' },
    client_lost: { level: 3, sound: '' },
    client_back: { level: 1, sound: '' },
    client_disconnected: { level: 0, sound: '' },
  },
  sendMode: 'any',
  clientSilentSeconds: 120,
  cooldownSeconds: 300,
  pushover: { enabled: false, userKey: '', appToken: '', sound: '' },
  ntfy: { enabled: false, server: 'https://ntfy.sh', topic: '', token: '' },
  webhook: { enabled: false, url: '', authHeader: '', insecureTls: false },
  mqtt: { enabled: false },
};

// Fill in fields a config doesn't carry.
export const mergeAlertsConfig = (source?: Partial<AlertsConfig>): AlertsConfig => ({
  ...defaultAlertsConfig,
  ...source,
  events: { ...defaultAlertsConfig.events, ...source?.events },
  pushover: { ...defaultAlertsConfig.pushover, ...source?.pushover },
  ntfy: { ...defaultAlertsConfig.ntfy, ...source?.ntfy },
  webhook: { ...defaultAlertsConfig.webhook, ...source?.webhook },
  mqtt: { ...defaultAlertsConfig.mqtt, ...source?.mqtt },
});

export const defaultHomeAssistant = { enabled: false, discoveryPrefix: 'homeassistant' };
