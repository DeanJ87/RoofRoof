import { mockConfig } from '../mocks/data';
import type { DepFacts } from '../lib/settingsDeps';

// One row per catalogued dependency the Settings UI shows (SQMeter spec 020, FR-013b);
// used by components/__tests__/SettingsDependencies.test.tsx.
export const alerts = mockConfig.alerts!;
export const level = (key: keyof typeof alerts.events, value: number) => ({
  ...alerts.events,
  [key]: { ...alerts.events[key], level: value },
});
export const mqttOn = { ...mockConfig.mqtt, enabled: true, broker: '192.168.1.10', topic: 'roofroof' };
export const mqttOff = { ...mockConfig.mqtt, enabled: false };

export type Case = {
  id: string;
  tab: string;
  config?: Record<string, unknown>;
  facts?: Partial<DepFacts>;
  note: string; // exact note text
  control?: string; // checkbox or select label
  control_state?: 'locked' | 'unlocked';
  fixLabel?: string;
  fixTab?: string; // the tab the fix lands on
};

export const CASES: Case[] = [
  {
    id: 'D-01',
    tab: 'alerts',
    config: { mqtt: mqttOff, alerts: { ...alerts, mqtt: { enabled: true } } },
    note: 'Inactive: MQTT is off',
    control: 'MQTT',
    control_state: 'unlocked',
    fixLabel: 'Turn on MQTT',
    fixTab: 'Network',
  },
  {
    id: 'D-01',
    tab: 'alerts',
    config: { mqtt: mqttOff, alerts: { ...alerts, mqtt: { enabled: false } } },
    note: 'MQTT is off.',
    control: 'MQTT',
    control_state: 'locked',
  },
  {
    id: 'D-02',
    tab: 'alerts',
    config: { mqtt: mqttOn, alerts: { ...alerts, mqtt: { enabled: true } } },
    facts: { mqttConnected: false },
    note: 'Inactive: Broker not connected',
    control: 'MQTT',
    control_state: 'unlocked',
  },
  {
    id: 'D-03',
    tab: 'alerts',
    config: { alerts: { ...alerts, ntfy: { ...alerts.ntfy, enabled: true } } },
    facts: { wifiConnected: false },
    note: 'Inactive: Not connected to WiFi',
    control: 'ntfy',
    control_state: 'unlocked',
    fixLabel: 'WiFi settings',
    fixTab: 'Network',
  },
  {
    id: 'D-12',
    tab: 'alerts',
    config: { alpaca: { ...mockConfig.alpaca, enabled: false }, alerts: { ...alerts, sendMode: 'whileConnected' } },
    note: 'Inactive: Alpaca is off',
    control: 'When to send',
    control_state: 'unlocked',
    fixLabel: 'Turn on Alpaca',
    fixTab: 'Network',
  },
  {
    id: 'D-13',
    tab: 'network',
    config: {
      mqtt: { ...mqttOn, homeAssistant: { enabled: true, discoveryPrefix: 'homeassistant' } },
      alerts: { ...alerts, enabled: false },
    },
    note: 'No alerts switch: Alerts are off',
    fixLabel: 'Turn on alerts',
    fixTab: 'Alerts',
  },
  {
    id: 'D-28',
    tab: 'time',
    config: { ntp: { ...mockConfig.ntp, enabled: true } },
    facts: { wifiConnected: false },
    note: 'Inactive: Not connected to WiFi',
    fixLabel: 'WiFi settings',
    fixTab: 'Network',
  },
  {
    id: 'D-32',
    tab: 'device',
    config: { ota: { enabled: true, password: '' } },
    note: 'Inactive: Set an upload password',
    control: 'ArduinoOTA uploads',
    control_state: 'unlocked',
  },
  {
    id: 'D-37',
    tab: 'alerts',
    config: { alpaca: { ...mockConfig.alpaca, enabled: false }, alerts: { ...alerts, events: level('client_lost', 3) } },
    note: 'Inactive: Alpaca is off',
    control: 'The imaging app stops checking: level',
    control_state: 'unlocked',
    fixLabel: 'Turn on Alpaca',
    fixTab: 'Network',
  },
  {
    id: 'D-37',
    tab: 'alerts',
    config: { alpaca: { ...mockConfig.alpaca, enabled: false }, alerts: { ...alerts, events: level('client_disconnected', 0) } },
    note: 'Alpaca is off.',
    control: 'The imaging app disconnects: level',
    control_state: 'locked',
  },
  {
    id: 'D-36',
    tab: 'network',
    config: { wifi: { ...mockConfig.wifi, mdns: true } },
    facts: { wifiConnected: false },
    note: 'Inactive: Not connected to WiFi',
    control: 'Advertise on the network (mDNS)',
    control_state: 'unlocked',
  },
  {
    id: 'D-37',
    tab: 'alerts',
    config: { alpaca: { ...mockConfig.alpaca, enabled: true }, alerts: { ...alerts, events: level('client_lost', 3) } },
    facts: { alpacaDevices: 0 },
    note: 'Inactive: No Alpaca device yet',
    control: 'The imaging app stops checking: level',
    control_state: 'unlocked',
  },
];
