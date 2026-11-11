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
  roofCommands?: boolean; // open, close and stop from <topic>/roof/set (spec 008), default off
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

// ASCOM Alpaca: discovery, the management API and the roof's Dome (spec 006).
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
  timeoutSeconds: number; // spec 005: the close sequence's wait for the park, 30-1800 s
}

// The roll-off roof's motion settings (spec 002).
export interface RoofConfig {
  travelMm: number;
  speedMmPerSecond: number;
  timeoutMarginSeconds: number;
  leaveMagnetSeconds: number;
  rampUpMs: number;
  rampDownMs: number;
  topDutyPercent: number;
  slowDutyPercent: number;
  openDirection: 'forward' | 'reverse';
  hallHighEnd: 'open' | 'closed';
  hallHighCounts: number;
  hallLowCounts: number;
  currentSenseEnabled: boolean;
  maxCurrentAmps: number;
  currentAmpsPerVolt: number; // A/V at the IBT-2's current-sense output
  minRunningAmps: number; // broken-sense-wire floor, 0 = off
  minSupplyVolts: number;
}

// The daylight failsafe and where the roof is (spec 013).
export type DaylightMode = 'off' | 'alert' | 'close';
export interface DaylightConfig {
  mode: DaylightMode;
  sunAltitudeDeg: number; // -18..30
}
export interface LocationConfig {
  set: boolean;
  latitude: number; // -90..90
  longitude: number; // -180..180
}

// The setup wizard (spec 015): new (opens once per page load), offered (a
// banner, after an upgrade) or done, and the page to resume at (0-9).
export type SetupState = 'new' | 'offered' | 'done';
export interface SetupConfig {
  state: SetupState;
  step: number;
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
  roof?: RoofConfig;
  daylight?: DaylightConfig;
  location?: LocationConfig;
  alerts?: AlertsConfig;
  // Absent on firmware before spec 015; Settings never sends it (the device keeps it).
  setup?: SetupConfig;
}
