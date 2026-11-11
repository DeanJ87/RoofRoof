import type { AlertsConfig } from './alerts';

export interface WiFiConfig {
  ssid: string;
  password: string;
  hostname: string;
  mdns?: boolean;
  ipv6?: boolean;
  autoReconnect: boolean;
  reconnectDelayMs: number;
  maxReconnectDelayMs: number;
}

export interface MQTTConfig {
  enabled: boolean;
  broker: string;
  port: number;
  username: string;
  password: string;
  topic: string; // base topic
  publishIntervalMs: number;
  homeAssistant?: { enabled: boolean; discoveryPrefix: string };
}

export interface OTAConfig {
  enabled: boolean;
  password: string;
}

export interface AuthConfig {
  enabled: boolean;
  username: string;
  password: string;
}

export interface NTPConfig {
  enabled: boolean;
  server1: string;
  server2: string;
  timezone: string; // POSIX, e.g. "GMT0BST,M3.5.0/1,M10.5.0"
  syncIntervalMs: number;
}

// ASCOM Alpaca: discovery, the management API and (spec 006) the Dome.
export interface AlpacaConfig {
  enabled: boolean;
}

// Where the weather verdict comes from (spec 004).
export interface SafetyConfig {
  http: { enabled: boolean; url: string };
  mqtt: { enabled: boolean; topic: string };
  staleSeconds: number;
  rainContact: boolean;
}

export type ParkSwitches = 'none' | 'one' | 'two';

// How the park is confirmed: N.I.N.A. AND the fitted switches (spec 004).
export interface ParkConfig {
  nina: { url: string; api: 'v2' };
  switches: ParkSwitches;
}

export interface Config {
  deviceName: string;
  wifi: WiFiConfig;
  mqtt: MQTTConfig;
  ota: OTAConfig;
  auth: AuthConfig;
  ntp: NTPConfig;
  alpaca?: AlpacaConfig;
  safety?: SafetyConfig;
  park?: ParkConfig;
  alerts?: AlertsConfig;
}
