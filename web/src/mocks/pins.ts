import type {
  AnalogConfig,
  AnalogIssue,
  BoardId,
  EthernetStatus,
  NetworkMode,
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
  parkAPullup: 'internal',
  parkBPullup: 'internal',
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
  parkAPullup: 'internal',
  parkBPullup: 'internal',
};

// The analog presets (lib/BoardLogic Analog.cpp): the DevKit bench wiring, and the WT32 carrier
// v1.1's, the same with +0.3 V on the supply for its reverse-polarity diode.
export const DEVKIT_ANALOG: AnalogConfig = {
  preset: 'devkit-bench',
  supply: { top: 100000, bottom: 22000, gain: 1, offsetVolts: 0 },
  hall: { top: 10000, bottom: 20000 },
  currentR: { top: 10000, bottom: 20000, gain: 1, offsetAmps: 0 },
  currentL: { top: 10000, bottom: 20000, gain: 1, offsetAmps: 0 },
};
export const WT32_ANALOG: AnalogConfig = {
  ...DEVKIT_ANALOG,
  preset: 'wt32-carrier-v1.1',
  supply: { ...DEVKIT_ANALOG.supply, offsetVolts: 0.3 },
};
const ANALOG_PRESETS = [DEVKIT_ANALOG, WT32_ANALOG];

const ANALOG_LABELS: Record<Exclude<AnalogIssue['input'], ''>, [string, number]> = {
  supply: ['Supply sense', 15],
  hall: ['Hall sensor', 4.2],
  currentR: ['R_IS', 5],
  currentL: ['L_IS', 5],
};
const resistorOk = (ohms: number) => Number.isInteger(ohms) && ohms >= 100 && ohms <= 10000000;

/** The device's checks on an analog setup (Analog.cpp checkAnalog), its texts included. */
export const mockAnalogIssues = (analog: AnalogConfig): AnalogIssue[] => {
  const issues: AnalogIssue[] = [];
  for (const input of ['supply', 'hall', 'currentR', 'currentL'] as const) {
    const [label, most] = ANALOG_LABELS[input];
    const { top, bottom } = analog[input];
    if (!resistorOk(top) || !resistorOk(bottom)) {
      issues.push({ input, error: true, text: `${label} divider: each resistor must be between 100 Ohm and 10 MOhm` });
      continue;
    }
    const atPin = most / ((top + bottom) / bottom);
    if (atPin > 3.3)
      issues.push({
        input,
        error: false,
        text: `${label} divider: at its highest (${most.toFixed(1)} V) the input would put ${atPin.toFixed(2)} V on the pin, over 3.3 V`,
      });
  }
  const trims = [
    ['supply', analog.supply.gain, analog.supply.offsetVolts, 1, 'V'],
    ['currentR', analog.currentR.gain, analog.currentR.offsetAmps, 0.5, 'A'],
    ['currentL', analog.currentL.gain, analog.currentL.offsetAmps, 0.5, 'A'],
  ] as const;
  for (const [input, gain, offset, most, unit] of trims) {
    const label = ANALOG_LABELS[input][0];
    if (!(gain >= 0.8 && gain <= 1.25)) issues.push({ input, error: true, text: `${label} trim: the gain must be between 0.8 and 1.25` });
    if (!(Math.abs(offset) <= most))
      issues.push({ input, error: true, text: `${label} trim: the offset must be within ${most.toFixed(1)} ${unit}` });
  }
  const named = ANALOG_PRESETS.find((preset) => preset.preset === analog.preset);
  if (named && JSON.stringify(named) !== JSON.stringify(analog))
    issues.push({ input: '', error: true, text: 'These values belong to a named preset; choose Custom to change one' });
  return issues;
};

type AnalogBody = Partial<Omit<AnalogConfig, 'supply' | 'hall' | 'currentR' | 'currentL'>> & {
  [K in 'supply' | 'hall' | 'currentR' | 'currentL']?: Partial<AnalogConfig[K]>;
};

/** An analog setup as the device reads it over `saved`: a named preset fills every value first. */
export const readAnalog = (body: AnalogBody, saved: AnalogConfig = DEVKIT_ANALOG): AnalogConfig => {
  const named = ANALOG_PRESETS.find((preset) => preset.preset === body.preset);
  const base = named ?? { ...saved, preset: body.preset ?? saved.preset };
  return {
    preset: base.preset,
    supply: { ...base.supply, ...body.supply },
    hall: { ...base.hall, ...body.hall },
    currentR: { ...base.currentR, ...body.currentR },
    currentL: { ...base.currentL, ...body.currentL },
  };
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
  const external = (fn === 'parkA' && map.parkAPullup === 'external') || (fn === 'parkB' && map.parkBPullup === 'external');
  if ((fn === 'parkA' || fn === 'parkB') && pin >= 34 && !external)
    return { function: fn, pin, code: 'noPullup', error: true, text: `${NAMES[fn]} on GPIO${pin}: GPIO${pin} has no internal pull-up` };
  if (fn !== 'enable' && STRAPPING.includes(pin)) return { function: fn, pin, code: `strap${pin}`, error: false, text: strapText(fn, pin) };
  return null;
};

export const BOARD_DETECTED_TEXT = 'This board is a WT32-ETH01 (its Ethernet answered): choose a WT32-ETH01 board';

/**
 * A simplified version of the device's rules: input-only outputs, shared pins and a park switch
 * on 34-39 without an external pull-up are errors, strapping pins warnings; on a device whose
 * Ethernet answered (`detected`), a map for another board is refused (`boardDetected`). With
 * `analog` in the body, the analog setup (read over `savedAnalog`) is checked too.
 */
export const mockValidate = (
  body: Partial<PinsConfig> & { analog?: AnalogBody },
  detected: BoardId | null = null,
  savedAnalog: AnalogConfig = DEVKIT_ANALOG,
): PinValidation => {
  const { analog: analogBody, ...pinsBody } = body;
  const pullups = { parkAPullup: pinsBody.parkAPullup ?? 'internal', parkBPullup: pinsBody.parkBPullup ?? 'internal' } as const;
  const map: PinsConfig =
    pinsBody.preset === 'wt32-carrier-v1'
      ? { ...WT32_PINS, ...pullups }
      : pinsBody.preset === 'devkit'
        ? { ...DEVKIT_PINS, ...pullups }
        : { ...DEVKIT_PINS, ...pinsBody };
  const seen = new Map<number, PinFunction>();
  const issues = FUNCTIONS.map((fn) => issueOf(map, fn, seen)).filter((issue): issue is PinIssue => issue !== null);
  if (detected === 'wt32-eth01' && map.board !== 'wt32-eth01')
    issues.unshift({ function: null, pin: 255, code: 'boardDetected', error: true, text: BOARD_DETECTED_TEXT });
  const valid = !issues.some((issue) => issue.error);
  if (!analogBody || typeof analogBody !== 'object') return { valid, pins: map, issues };
  const analog = readAnalog(analogBody, savedAnalog);
  const analogIssues = mockAnalogIssues(analog);
  return { valid: valid && !analogIssues.some((issue) => issue.error), pins: map, issues, analog, analogIssues };
};

export type MockBoard = 'devkit' | 'wt32' | 'unset';

const savedPins = (kind: MockBoard): PinsConfig =>
  kind === 'wt32' ? WT32_PINS : kind === 'unset' ? { ...DEVKIT_PINS, preset: 'unset' } : DEVKIT_PINS;

/**
 * GET /api/pins for a DevKit, a WT32-ETH01 on its carrier, or a new device whose boot probe
 * found a WT32-ETH01 (the board in effect is then the detected one).
 */
export const pinsDocumentFor = (kind: MockBoard): PinsDocument => {
  const pins = savedPins(kind);
  const validation = mockValidate(pins);
  const analog = kind === 'wt32' ? WT32_ANALOG : DEVKIT_ANALOG;
  return {
    running: pins,
    saved: pins,
    restartNeeded: false,
    valid: kind !== 'unset',
    reason: kind === 'unset' ? 'No board chosen yet: choose the board and its pins' : '',
    issues: kind === 'unset' ? [] : validation.issues,
    psram: false,
    board: kind === 'devkit' ? 'esp32-devkit' : 'wt32-eth01',
    detected: kind === 'unset' ? 'wt32-eth01' : null,
    presets: [DEVKIT_PINS, WT32_PINS],
    boards: BOARDS,
    // The DevKit default's two warnings: 10k/20k puts 3.33 V on R_IS and L_IS at the BTS7960's 5 V.
    analog,
    analogIssues: mockAnalogIssues(analog),
    analogPresets: ANALOG_PRESETS,
  };
};

const NO_ETHERNET: Omit<EthernetStatus, 'mode'> = {
  present: false,
  started: false,
  phyFound: false,
  link: false,
  speedMbps: 0,
  fullDuplex: false,
  ip: '',
  mac: '',
  ipv6: [],
};

/**
 * The status's `ethernet` block: up on a WT32-ETH01 in `mode` (in effect since boot), not
 * started in WiFi-only mode, absent hardware (mode `wifi`) on a DevKit.
 */
export const ethernetFor = (board: BoardId, mode: NetworkMode = 'auto'): EthernetStatus =>
  board === 'wt32-eth01' && mode !== 'wifi'
    ? {
        mode,
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
    : { ...NO_ETHERNET, mode: 'wifi' };

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
