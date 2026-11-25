import { describe, expect, it } from 'vitest';
import { tipPlace } from '../tipPlace';

// Spec 028 FR-011: a tip is drawn beside its "?" and kept inside the window.
const view = { width: 400, height: 800 };
const tip = { width: 200, height: 60 };
const at = (top: number, left: number) => ({ top, bottom: top + 16, left, width: 16 });

describe('tipPlace', () => {
  it('centres the tip above its "?"', () => {
    expect(tipPlace(at(300, 192), tip, view)).toEqual({ top: 300 - 7 - 60, left: 100 });
  });

  it('goes below when there is no room above', () => {
    expect(tipPlace(at(20, 192), tip, view).top).toBe(20 + 16 + 7);
  });

  it('stays inside the window at either edge', () => {
    expect(tipPlace(at(300, 2), tip, view).left).toBe(8);
    expect(tipPlace(at(300, 390), tip, view).left).toBe(400 - 200 - 8);
  });
});
