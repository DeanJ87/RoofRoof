import { describe, expect, it } from 'vitest';
import { formatInputNumber } from '../format';

// Spec 026: a float the device keeps (38.8 reads back as 38.79999924) shows as it was typed.
describe('formatInputNumber', () => {
  it('shows a 32-bit float as typed; other numbers unchanged', () => {
    expect(formatInputNumber(38.79999924, true)).toBe('38.8');
    expect(formatInputNumber(38.79999924)).toBe('38.79999924');
    expect(formatInputNumber(1850, true)).toBe('1850');
  });
});
