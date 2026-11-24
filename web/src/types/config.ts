import type { AlertsConfig } from './alerts';
import type { AnalogConfig, NetworkConfig, PinsConfig } from './pins';

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
  password: string | null; // null clears the saved password (spec 020: a copied broker)
  topic: string; // base topic
  publishIntervalMs: number;
  homeAssistant?: { enabled: boolean; discoveryPrefix: string };
  roofCommands?: boolean; // open, close and stop from <topic>/roof/set (spec 008), default off
  changeMinSeconds?: number; // spec 014: at rest, a change is published at most this often (1..60 s)
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
  // Spec 020: SQMeter's login (HTTP Basic), absent on older firmware; the password is masked, null clears it.
  http: { enabled: boolean; url: string; username?: string; password?: string | null };
  mqtt: { enabled: boolean; topic: string };
  staleSeconds: number;
}

export type ParkSwitches = 'none' | 'one' | 'two';
// Spec 024: what confirms the park. Absent on older firmware: none → nina, one or two → ninaAndSwitches.
export type ParkSource = 'nina' | 'switches' | 'ninaAndSwitches';

// How the park is confirmed (spec 004): N.I.N.A., the fitted switches, or both (spec 024).
export interface ParkConfig {
  nina: { url: string; api: 'v2' };
  source?: ParkSource;
  switches: ParkSwitches;
  timeoutSeconds: number; // spec 005: the close sequence's wait for the park, 30-1800 s
  requestPark?: boolean; // spec 022: the automatic close may ask N.I.N.A. to park (absent on older firmware: on)
}

// The roll-off roof's motion settings (spec 002).
export interface RoofConfig {
  travelMm: number;
  speedMmPerSecond: number;
  timeoutMarginSeconds: number;
  leaveMagnetSeconds: number;
  calibrationLimitSeconds: number;
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
  bandDeg?: number; // spec 014: night again only this far below the threshold (0.1..2)
}
// Spec 020: the location follows SQMeter's (`sqmeter`, the default) or is typed in (`manual`).
export type LocationSource = 'sqmeter' | 'manual';
export interface LocationConfig {
  source?: LocationSource; // absent before spec 020: `manual` when set, else `sqmeter`
  set: boolean;
  latitude: number; // -90..90
  longitude: number; // -180..180
  // Written by the device, ignored on a save: the kept value was SQMeter's GPS fix, and when it was saved (Unix s, 0 never).
  sqmeter?: { gps: boolean; savedAt: number };
}

// Spec 020: each group of site settings follows SQMeter's, or is set here.
// `syncedAt` is the device's (Unix s of the last sync, 0 never) and ignored on a save.
export type SyncGroup = 'time' | 'mqtt' | 'alerts';
export interface SyncGroupConfig {
  follow: boolean;
  syncedAt: number;
}
export type SqmeterSyncConfig = Record<SyncGroup, SyncGroupConfig>;

// The setup wizard (spec 015): new (opens once per page load), offered (a
// banner, after an upgrade) or done, and the page to resume at (0-9).
export type SetupState = 'new' | 'offered' | 'done';
export interface SetupConfig {
  state: SetupState;
  step: number;
}
// The automatic close's policy (spec 014): specs 005 and 013's fixed values as settings.
export type NotStartedPolicy = 'halt' | 'retry';
export interface AutoCloseConfig {
  rearmMinutes: number; // 5..60
  parkRequestSeconds: number; // 10..120
  parkHoldSeconds: number; // 4..30
  retryMinutes: number; // 1..30
  closeStartSeconds: number; // 25..120
  closeGuardSeconds: number; // 10..120
  notStarted: NotStartedPolicy;
  remindMinutes: number; // 10..120
  remoteStop: boolean;
}
export interface UiConfig {
  confirmOpenClose: boolean; // Open and Close ask first; Stop never does
}
// Advanced diagnostics (spec 017): all off by default; the other two need `enabled`.
export interface DiagConfig {
  enabled: boolean;
  keepSummaries: boolean; // move summaries kept across restarts
  mqtt: boolean; // <base>/diag and <base>/diag/move, at rest only
}

// Spec 022: the test box's bypasses (constitution 3.0.0). `until` is the
// device's deadline (Unix seconds, 0 none): never sent, set by the save that
// turns a bypass on or sends `hours`.
export interface TestBoxConfig {
  park: boolean; // no telescope under this roof: stands in for N.I.N.A.
  weather: boolean; // stands in for a missing weather source
  daylight: boolean; // no daylight close or open refusal
  hours: number; // 1..720
  until: number;
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
  sqmeterSync?: SqmeterSyncConfig; // absent before spec 020's sync: every group set here
  autoClose?: AutoCloseConfig;
  ui?: UiConfig;
  alerts?: AlertsConfig;
  diag?: DiagConfig; // absent on firmware before spec 017
  // Spec 023: the board and its pins, and the network mode on a board with Ethernet (both apply at a restart).
  pins?: PinsConfig;
  network?: NetworkConfig;
  // Spec 023 Advanced: the analog inputs' dividers and trims; applied once the roof is still, no restart.
  analog?: AnalogConfig;
  testBox?: TestBoxConfig; // absent on firmware before spec 022
  // Absent on firmware before spec 015; Settings never sends it (the device keeps it).
  setup?: SetupConfig;
}
