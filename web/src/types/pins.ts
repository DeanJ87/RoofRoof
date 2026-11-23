// The board, its pins and the wiring check (spec 023, contracts/pins-api.md).

// `unset`: a new device with no board chosen drives nothing until one is chosen and it restarts.
export type PinPreset = 'devkit' | 'wt32-carrier-v1' | 'custom' | 'unset';
export type BoardId = 'esp32-devkit' | 'wt32-eth01';
export type PinFunction = 'rpwm' | 'lpwm' | 'enable' | 'currentR' | 'currentL' | 'supply' | 'hall' | 'parkA' | 'parkB';

// A park input's pull-up: the chip's own, or one fitted on the board (then it may be on GPIO34-39).
export type Pullup = 'internal' | 'external';

// The settings' `pins` group: a named preset fixes the board and every pin; the
// pull-ups aren't part of a preset (absent before spec 023's Advanced section).
export type PinsConfig = { preset: PinPreset; board: BoardId; parkAPullup?: Pullup; parkBPullup?: Pullup } & Record<PinFunction, number>;

// The settings' `analog` group (spec 023, Advanced): each analog input's divider
// (ohms) and, for the supply and the current sense, a trim. A named preset fixes them all.
export type AnalogPreset = 'devkit-bench' | 'wt32-carrier-v1.1' | 'custom';
export type AnalogInput = 'supply' | 'hall' | 'currentR' | 'currentL';
export interface Divider {
  top: number;
  bottom: number;
}
export interface AnalogConfig {
  preset: AnalogPreset;
  supply: Divider & { gain: number; offsetVolts: number };
  hall: Divider;
  currentR: Divider & { gain: number; offsetAmps: number };
  currentL: Divider & { gain: number; offsetAmps: number };
}

/** One of the device's checks on the analog setup: an error blocks a save, a warning doesn't. */
export interface AnalogIssue {
  input: AnalogInput | '';
  error: boolean;
  text: string;
}

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
  // Spec 023 Advanced: the saved analog setup, the device's checks on it, and the named presets' values.
  analog?: AnalogConfig;
  analogIssues?: AnalogIssue[];
  analogPresets?: AnalogConfig[];
}

/** POST /api/pins/validate. */
export interface PinValidation {
  valid: boolean;
  pins: PinsConfig;
  issues: PinIssue[];
  // With `analog` in the body: the analog setup as read and its checks (`valid` covers them too).
  analog?: AnalogConfig;
  analogIssues?: AnalogIssue[];
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
