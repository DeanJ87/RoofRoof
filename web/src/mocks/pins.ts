import type {
  BoardId,
  EthernetStatus,
  PinFunction,
  PinIssue,
  PinsConfig,
  PinsDocument,
  PinValidation,
  WiringCheckDocument,
} from '../types';

// The pin routes (spec 023, contracts/pins-api.md) as a device would answer
// them: a DevKit with its preset chosen by default; tests ask for a WT32-ETH01
// on its carrier or a new device with no board chosen (`pinsDocumentFor`).

export const DEVKIT_PINS: PinsConfig = {
  preset: 'devkit',
  board: 'esp32-devkit',
  rpwm: 25,
  lpwm: 26,
  enable: 27,
  currentR: 34,
  currentL: 35,
  supply: 36,
  hall: 39,
  parkA: 32,
  parkB: 33,
};

export const WT32_PINS: PinsConfig = {
  preset: 'wt32-carrier-v1',
  board: 'wt32-eth01',
  rpwm: 14,
  lpwm: 15,
  enable: 4,
  currentR: 35,
  currentL: 32,
  supply: 36,
  hall: 39,
  parkA: 33,
  parkB: 5,
};

const BOARDS: PinsDocument['boards'] = [
  { board: 'esp32-devkit', ethernet: [], header: [] },
  { board: 'wt32-eth01', ethernet: [0, 16, 18, 19, 21, 22, 23, 25, 26, 27], header: [2, 4, 5, 12, 14, 15, 17, 32, 33, 35, 36, 39] },
];

const NAMES: Record<PinFunction, string> = {
  rpwm: 'RPWM',
  lpwm: 'LPWM',
  enable: 'EN',
  currentR: 'R_IS',
  currentL: 'L_IS',
  supply: 'Supply sense',
  hall: 'Hall sensor',
  parkA: 'Park switch A',
  parkB: 'Park switch B',
};
const FUNCTIONS = Object.keys(NAMES) as PinFunction[];
const OUTPUTS: PinFunction[] = ['rpwm', 'lpwm', 'enable'];
const STRAPPING = [0, 2, 5, 12, 15];

const strapText = (fn: PinFunction, pin: number) =>
  `${NAMES[fn]} on GPIO${pin}: a strapping pin that pulses while the chip starts; fine for a switch input, or a PWM input while EN is held low`;

const issueOf = (map: PinsConfig, fn: PinFunction, seen: Map<number, PinFunction>): PinIssue | null => {
  const pin = map[fn];
  const owner = seen.get(pin);
  if (owner)
    return { function: fn, pin, code: 'shared', error: true, text: `${NAMES[fn]} on GPIO${pin}: GPIO${pin} is already ${NAMES[owner]}` };
  seen.set(pin, fn);
  if (OUTPUTS.includes(fn) && pin >= 34)
    return {
      function: fn,
      pin,
      code: 'inputOnly',
      error: true,
      text: `${NAMES[fn]} on GPIO${pin}: GPIO${pin} is input only and can't drive an output`,
    };
  if (fn !== 'enable' && STRAPPING.includes(pin)) return { function: fn, pin, code: `strap${pin}`, error: false, text: strapText(fn, pin) };
  return null;
};

/** A simplified version of the device's rules: input-only outputs and shared pins are errors, strapping pins warnings. */
export const mockValidate = (body: Partial<PinsConfig>): PinValidation => {
  const map: PinsConfig =
    body.preset === 'wt32-carrier-v1' ? WT32_PINS : body.preset === 'devkit' ? DEVKIT_PINS : { ...DEVKIT_PINS, ...body };
  const seen = new Map<number, PinFunction>();
  const issues = FUNCTIONS.map((fn) => issueOf(map, fn, seen)).filter((issue): issue is PinIssue => issue !== null);
  return { valid: !issues.some((issue) => issue.error), pins: map, issues };
};

export type MockBoard = 'devkit' | 'wt32' | 'unset';

const savedPins = (kind: MockBoard): PinsConfig =>
  kind === 'wt32' ? WT32_PINS : kind === 'unset' ? { ...DEVKIT_PINS, preset: 'unset' } : DEVKIT_PINS;

/** GET /api/pins for a DevKit, a WT32-ETH01 on its carrier, or a new device whose boot probe found a WT32-ETH01. */
export const pinsDocumentFor = (kind: MockBoard): PinsDocument => {
  const pins = savedPins(kind);
  const validation = mockValidate(pins);
  return {
    running: pins,
    saved: pins,
    restartNeeded: false,
    valid: kind !== 'unset',
    reason: kind === 'unset' ? 'No board chosen yet' : '',
    issues: kind === 'unset' ? [] : validation.issues,
    psram: false,
    board: kind === 'wt32' ? 'wt32-eth01' : 'esp32-devkit',
    detected: kind === 'unset' ? 'wt32-eth01' : null,
    presets: [DEVKIT_PINS, WT32_PINS],
    boards: BOARDS,
  };
};

/** The status's `ethernet` block: up on a WT32-ETH01, absent hardware on a DevKit. */
export const ethernetFor = (board: BoardId): EthernetStatus =>
  board === 'wt32-eth01'
    ? {
        present: true,
        started: true,
        phyFound: true,
        link: true,
        speedMbps: 100,
        fullDuplex: true,
        ip: '192.168.1.60',
        mac: 'A1:B2:C3:D4:E5:F7',
        ipv6: ['fe80::a3b2:c3ff:fed4:e5f7'],
      }
    : { present: false, started: false, phyFound: false, link: false, speedMbps: 0, fullDuplex: false, ip: '', mac: '', ipv6: [] };

/** GET /api/pins/check once a check has run on a DevKit with nothing fitted but the supply and the Hall sensor. */
export const mockWiringDone: WiringCheckDocument = {
  state: 'done',
  checks: [
    {
      id: 'enPulldown',
      level: 'pass',
      value: null,
      unit: '',
      text: 'EN reads low with the internal pull-up: its pull-down holds the driver off',
    },
    { id: 'hall', level: 'pass', value: 2010, unit: 'counts', text: '2010 counts: between the rails' },
    { id: 'supply', level: 'pass', value: 12.4, unit: 'V', text: '12.4 V: a plausible supply' },
    { id: 'parkA', level: 'skipped', value: null, unit: '', text: 'Not fitted (Settings, Roof, park switches)' },
    { id: 'currentR', level: 'warn', value: 140, unit: 'mV', text: '140 mV while stopped: check the R_IS wire' },
    { id: 'ethernet', level: 'pass', value: null, unit: '', text: 'No Ethernet PHY found, as expected on an ESP32 DevKit' },
    {
      id: 'motion',
      level: 'skipped',
      value: null,
      unit: '',
      text: 'Direction and travel need the roof to move: use Jog and teach the ends on the calibration page',
    },
  ],
  finishedAt: 123456,
};

// What the mock device's check has done since "boot": tests reset it.
export const mockWiring: { state: WiringCheckDocument } = { state: { state: 'idle', checks: [] } };
