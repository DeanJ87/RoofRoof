import { describe, expect, it } from 'vitest';
import { calibrateSupplyGain, dividerRatio, pinVoltsAtMax, withAnalogPreset } from '../analogView';
import { DEVKIT_ANALOG, WT32_ANALOG, pinsDocumentFor } from '../../mocks/pins';

// Spec 023 Advanced: the analog front end as the Pins page works it out
// (mirroring lib/BoardLogic Analog.cpp).

describe('analogView', () => {
  it('works out a divider ratio and the pin voltage at the sensor maximum', () => {
    expect(dividerRatio({ top: 100000, bottom: 22000 })).toBeCloseTo(5.545, 3);
    expect(dividerRatio({ top: 10000, bottom: 0 })).toBeNull();
    expect(dividerRatio({ top: Number.NaN, bottom: 1000 })).toBeNull();
    expect(pinVoltsAtMax('supply', { top: 100000, bottom: 22000 })).toBeCloseTo(2.705, 3);
    expect(pinVoltsAtMax('hall', { top: 10000, bottom: 20000 })).toBeCloseTo(2.8, 3);
    // The drawn 10k/20k on the BTS7960's 5 V IS: over 3.3 V (the device warns).
    expect(pinVoltsAtMax('currentR', { top: 10000, bottom: 20000 })).toBeCloseTo(3.333, 3);
    expect(pinVoltsAtMax('currentL', { top: 10000, bottom: 22000 })).toBeCloseTo(3.4375, 4);
  });

  it('chooses an analog preset: a named one takes its values, Custom keeps the current ones', () => {
    const doc = pinsDocumentFor('devkit');
    expect(withAnalogPreset(doc, DEVKIT_ANALOG, 'wt32-carrier-v1.1')).toEqual(WT32_ANALOG);
    expect(withAnalogPreset(doc, WT32_ANALOG, 'custom')).toEqual({ ...WT32_ANALOG, preset: 'custom' });
  });
});

const BENCH = { top: 100000, bottom: 22000, gain: 1, offsetVolts: 0 };
const supply = (gain: number, offsetVolts: number) => ({ ...BENCH, gain, offsetVolts });

describe('calibrateSupplyGain (Analog.cpp calibratedSupplyGain)', () => {
  it('sets the gain that makes the reading match the measured voltage, keeping the offset', () => {
    expect(calibrateSupplyGain(BENCH, BENCH, 12.46, 12.8)).toEqual({ ok: true, gain: 1.0273 });
    // With an offset (the WT32 carrier's diode) and a gain already set: the untrimmed reading is scaled.
    // Untrimmed: (12.3 - 0.3) / 1.1 = 10.909; measured 12.8: (12.8 - 0.3) / 10.909 = 1.1458.
    expect(calibrateSupplyGain(supply(1.1, 0.3), supply(1.1, 0.3), 12.3, 12.8)).toEqual({ ok: true, gain: 1.1458 });
  });

  it('works from the saved setup that scaled the reading, for the setup as edited', () => {
    // The WT32 preset chosen but not saved: the reading has no +0.3 V in it, so the gain makes room for it.
    expect(calibrateSupplyGain(BENCH, supply(1, 0.3), 12, 12)).toEqual({ ok: true, gain: 0.975 });
    // A divider edited but not saved: 100k/20k (x6) in place of 100k/22k (x5.545).
    const answer = calibrateSupplyGain(BENCH, { ...BENCH, bottom: 20000 }, 12, 12);
    expect(answer).toEqual({ ok: true, gain: 0.9242 });
  });

  it('refuses with no reading, a reading under 1 V, a measured voltage not above the offset, or a gain outside 0.8 to 1.25', () => {
    expect(calibrateSupplyGain(BENCH, BENCH, null, 12)).toEqual({ ok: false, reason: 'noReading' });
    expect(calibrateSupplyGain(BENCH, BENCH, undefined, 12)).toEqual({ ok: false, reason: 'noReading' });
    expect(calibrateSupplyGain(BENCH, BENCH, 0.6, 12)).toEqual({ ok: false, reason: 'noReading' });
    expect(calibrateSupplyGain(BENCH, { ...BENCH, bottom: 0 }, 12, 12)).toEqual({ ok: false, reason: 'noReading' });
    expect(calibrateSupplyGain(BENCH, supply(1, 0.3), 12, 0.2)).toEqual({ ok: false, reason: 'measured' });
    expect(calibrateSupplyGain(BENCH, BENCH, 12, Number.NaN)).toEqual({ ok: false, reason: 'measured' });
    expect(calibrateSupplyGain(BENCH, BENCH, 12, 15.1)).toEqual({ ok: false, reason: 'range' });
    expect(calibrateSupplyGain(BENCH, BENCH, 12, 9.5)).toEqual({ ok: false, reason: 'range' });
    // The limits themselves are allowed.
    expect(calibrateSupplyGain(BENCH, BENCH, 12, 15)).toEqual({ ok: true, gain: 1.25 });
    expect(calibrateSupplyGain(BENCH, BENCH, 12, 9.6)).toEqual({ ok: true, gain: 0.8 });
  });
});
