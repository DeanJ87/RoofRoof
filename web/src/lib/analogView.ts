import type { AnalogConfig, AnalogInput, AnalogIssue, AnalogPreset, Divider, PinPreset, PinsDocument } from '../types';

// The analog front end on the Pins page's Advanced section (spec 023): the
// dividers' ratios, what each puts on its pin, the presets, and calibrating the
// supply to a measured voltage. Pure: the device decides what is allowed
// (lib/BoardLogic Analog.cpp, mirrored here for what the page shows).

export const ANALOG_INPUTS: AnalogInput[] = ['supply', 'hall', 'currentR', 'currentL'];

// The most each sensor puts out (Analog.h): a 12 V system's supply at its highest, the SS49E on 5 V, the BTS7960's IS.
export const SENSOR_MAX_VOLTS: Record<AnalogInput, number> = { supply: 15, hall: 4.2, currentR: 5, currentL: 5 };
export const ADC_SAFE_VOLTS = 3.3;
export const GAIN_RANGE = { min: 0.5, max: 2 } as const;
// Below this the supply reading is too small to scale from (a supply not connected).
export const MIN_CALIBRATE_READING_VOLTS = 1;

/** Input volts per pin volt; null when the divider can't be used. */
export const dividerRatio = (divider: Divider): number | null =>
  Number.isFinite(divider.top) && Number.isFinite(divider.bottom) && divider.bottom > 0 && divider.top >= 0
    ? (divider.top + divider.bottom) / divider.bottom
    : null;

/** The pin's voltage with the input at its sensor's maximum; null when the divider can't be used. */
export const pinVoltsAtMax = (input: AnalogInput, divider: Divider): number | null => {
  const ratio = dividerRatio(divider);
  return ratio === null ? null : SENSOR_MAX_VOLTS[input] / ratio;
};

/** The analog preset that goes with a board preset: the DevKit's bench wiring or the WT32 carrier's. */
export const analogPresetFor = (preset: PinPreset): AnalogPreset | null =>
  preset === 'devkit' ? 'devkit-bench' : preset === 'wt32-carrier-v1' ? 'wt32-carrier-v1.1' : null;

/** A named analog preset's values, from the device's list. */
export const analogPresetValues = (doc: PinsDocument | null, preset: AnalogPreset): AnalogConfig | null =>
  doc?.analogPresets?.find((analog) => analog.preset === preset) ?? null;

/** The analog setup after choosing `preset`: a named one's values, or the current values made custom. */
export const withAnalogPreset = (doc: PinsDocument | null, analog: AnalogConfig, preset: AnalogPreset): AnalogConfig =>
  preset === 'custom' ? { ...analog, preset } : (analogPresetValues(doc, preset) ?? { ...analog, preset });

/**
 * The analog setup to go with a board preset just chosen: the matching named preset when the
 * current one is named too (a Custom setup is left alone), or null when nothing changes.
 */
export const analogForBoard = (doc: PinsDocument | null, analog: AnalogConfig | undefined, board: PinPreset): AnalogConfig | null => {
  const preset = analogPresetFor(board);
  if (!analog || !preset || analog.preset === 'custom' || analog.preset === preset) return null;
  return withAnalogPreset(doc, analog, preset);
};

export const analogErrors = (issues: AnalogIssue[]) => issues.filter((issue) => issue.error);

export type CalibrateResult = { ok: true; gain: number } | { ok: false; reason: 'noReading' | 'measured' | 'range' };

/**
 * The supply gain that makes the reading `nowVolts` (scaled with the current gain and offset) read
 * as `measuredVolts`, keeping the offset (Analog.cpp calibratedSupplyGain). Refused when the
 * reading is missing or under 1 V, the measured value isn't above the offset, or the gain would be
 * outside 0.5 to 2. Rounded to 4 decimals.
 */
export const calibrateSupplyGain = (
  trim: { gain: number; offsetVolts: number },
  nowVolts: number | null | undefined,
  measuredVolts: number,
): CalibrateResult => {
  if (nowVolts === null || nowVolts === undefined || !Number.isFinite(nowVolts) || nowVolts < MIN_CALIBRATE_READING_VOLTS)
    return { ok: false, reason: 'noReading' };
  const untrimmed = (nowVolts - trim.offsetVolts) / trim.gain;
  if (!Number.isFinite(untrimmed) || untrimmed <= 0.1) return { ok: false, reason: 'noReading' };
  if (!Number.isFinite(measuredVolts) || measuredVolts <= trim.offsetVolts) return { ok: false, reason: 'measured' };
  const gain = Math.round(((measuredVolts - trim.offsetVolts) / untrimmed) * 10000) / 10000;
  if (gain < GAIN_RANGE.min || gain > GAIN_RANGE.max) return { ok: false, reason: 'range' };
  return { ok: true, gain };
};
