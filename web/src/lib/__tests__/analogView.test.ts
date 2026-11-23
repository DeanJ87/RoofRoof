import { describe, expect, it } from 'vitest';
import { analogForBoard, analogPresetFor, calibrateSupplyGain, dividerRatio, pinVoltsAtMax, withAnalogPreset } from '../analogView';
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

  it('pairs board presets with analog presets; a Custom analog setup stays', () => {
    const doc = pinsDocumentFor('devkit');
    expect(analogPresetFor('devkit')).toBe('devkit-bench');
    expect(analogPresetFor('wt32-carrier-v1')).toBe('wt32-carrier-v1.1');
    expect(analogPresetFor('custom')).toBeNull();
    expect(analogForBoard(doc, DEVKIT_ANALOG, 'wt32-carrier-v1')).toEqual(WT32_ANALOG);
    expect(analogForBoard(doc, WT32_ANALOG, 'devkit')).toEqual(DEVKIT_ANALOG);
    expect(analogForBoard(doc, DEVKIT_ANALOG, 'devkit')).toBeNull();
    expect(analogForBoard(doc, DEVKIT_ANALOG, 'custom')).toBeNull();
    expect(analogForBoard(doc, { ...DEVKIT_ANALOG, preset: 'custom' }, 'wt32-carrier-v1')).toBeNull();
    expect(analogForBoard(doc, undefined, 'wt32-carrier-v1')).toBeNull();
    expect(withAnalogPreset(doc, WT32_ANALOG, 'custom')).toEqual({ ...WT32_ANALOG, preset: 'custom' });
  });
});

describe('calibrateSupplyGain (Analog.cpp calibratedSupplyGain)', () => {
  it('sets the gain that makes the reading match the measured voltage, keeping the offset', () => {
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, 12.46, 12.8)).toEqual({ ok: true, gain: 1.0273 });
    // With an offset (the WT32 carrier's diode) and a gain already set: the untrimmed reading is scaled.
    // Untrimmed: (12.3 - 0.3) / 1.2 = 10; measured 12.8: (12.8 - 0.3) / 10 = 1.25.
    expect(calibrateSupplyGain({ gain: 1.2, offsetVolts: 0.3 }, 12.3, 12.8)).toEqual({ ok: true, gain: 1.25 });
  });

  it('refuses with no reading, a reading under 1 V, a measured voltage not above the offset, or a gain outside 0.5 to 2', () => {
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, null, 12)).toEqual({ ok: false, reason: 'noReading' });
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, undefined, 12)).toEqual({ ok: false, reason: 'noReading' });
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, 0.6, 12)).toEqual({ ok: false, reason: 'noReading' });
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0.3 }, 12, 0.2)).toEqual({ ok: false, reason: 'measured' });
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, 12, Number.NaN)).toEqual({ ok: false, reason: 'measured' });
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, 12, 30)).toEqual({ ok: false, reason: 'range' });
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, 12, 5)).toEqual({ ok: false, reason: 'range' });
    // The limits themselves are allowed.
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, 12, 24)).toEqual({ ok: true, gain: 2 });
    expect(calibrateSupplyGain({ gain: 1, offsetVolts: 0 }, 12, 6)).toEqual({ ok: true, gain: 0.5 });
  });
});
