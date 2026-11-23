// The board, its pins and the wiring check (spec 023, contracts/pins-api.md).

// `unset`: a new device with no board chosen drives nothing until one is chosen and it restarts.
export type PinPreset = 'devkit' | 'wt32-carrier-v1' | 'custom' | 'unset';
export type BoardId = 'esp32-devkit' | 'wt32-eth01';
export type PinFunction = 'rpwm' | 'lpwm' | 'enable' | 'currentR' | 'currentL' | 'supply' | 'hall' | 'parkA' | 'parkB';

// The settings' `pins` group: a named preset fixes the board and every pin.
export type PinsConfig = { preset: PinPreset; board: BoardId } & Record<PinFunction, number>;

// What happens to the network on a board with Ethernet: Ethernet with WiFi as a fallback, or one of them only.
export type NetworkMode = 'auto' | 'ethernet' | 'wifi';
export interface NetworkConfig {
  mode: NetworkMode;
}

/** One broken rule (`error`, blocks a save) or warning about a pin; `text` is the device's English. */
export interface PinIssue {
  function: PinFunction | null;
  pin: number;
  code: string;
  error: boolean;
  text: string;
}

export interface BoardInfo {
  board: BoardId;
  ethernet: number[]; // the pins Ethernet uses
  header: number[]; // the pins on the board's header; [] means every GPIO
}

/** GET /api/pins. */
export interface PinsDocument {
  running: PinsConfig; // read at boot: in effect until the next restart
  saved: PinsConfig;
  restartNeeded: boolean;
  valid: boolean;
  reason: string;
  issues: PinIssue[];
  psram: boolean;
  board: BoardId; // the board in effect
  detected: BoardId | null; // with no board chosen: what the boot probe found
  presets: PinsConfig[];
  boards: BoardInfo[];
}

/** POST /api/pins/validate. */
export interface PinValidation {
  valid: boolean;
  pins: PinsConfig;
  issues: PinIssue[];
}

export type WiringCheckId = 'enPulldown' | 'hall' | 'supply' | 'parkA' | 'parkB' | 'currentR' | 'currentL' | 'ethernet' | 'motion';
export type WiringLevel = 'pass' | 'warn' | 'fail' | 'skipped';

export interface WiringCheckResult {
  id: WiringCheckId;
  level: WiringLevel;
  value: number | null;
  unit: string;
  text: string;
}

/** GET /api/pins/check. */
export interface WiringCheckDocument {
  state: 'idle' | 'running' | 'done';
  checks: WiringCheckResult[];
  suggestPreset?: 'wt32-carrier-v1';
  finishedAt?: number;
}

/** The status's `ethernet` block (absent before spec 023). */
export interface EthernetStatus {
  // The network mode in effect since boot: `wifi` on a board without Ethernet and in WiFi-only mode.
  mode: NetworkMode;
  present: boolean; // Ethernet is in use: false on a board without it and in WiFi-only mode
  started: boolean;
  phyFound: boolean;
  link: boolean;
  speedMbps: number;
  fullDuplex: boolean;
  ip: string;
  mac: string;
  ipv6: string[];
}

/** The status's `pins` block (absent before spec 023). */
export interface PinsStatus {
  preset: PinPreset;
  board: BoardId;
  valid: boolean;
  restartNeeded: boolean;
}
