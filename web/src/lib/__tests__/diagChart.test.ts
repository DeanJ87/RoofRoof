import { describe, expect, it } from 'vitest';
import { LiveBuffer, linePath, nearestIndex, niceScale, summarise, xOf, yOf } from '../diagChart';
import { mockDiagLive, mockDiagMove } from '../../mocks/diag';
import type { DiagLive, DiagPoint } from '../../types/diag';

// The Diagnostics chart's helpers (spec 017 FR-034): round scales, paths that
// break at a missing reading, and a live buffer that stays bounded.

const reading = (id: number, elapsedMs: number | null, active = true, amps: number | null = 3): DiagLive => ({
  ...mockDiagLive,
  roof: { ...mockDiagLive.roof, elapsedMs, motorAmps: amps },
  move: { id, active },
});

describe('niceScale', () => {
  it('rounds the range to whole steps and keeps zero in view when asked', () => {
    const scale = niceScale([1.2, 4.8, null], { floor: 0 });
    expect(scale.min).toBe(0);
    expect(scale.max).toBe(6);
    expect(scale.ticks).toEqual([0, 2, 4, 6]);
  });

  it('spreads a flat or empty series instead of dividing by zero', () => {
    expect(niceScale([12, 12]).max).toBeGreaterThan(niceScale([12, 12]).min);
    expect(niceScale([])).toMatchObject({ min: 0, max: 1 });
  });

  it('gives the ticks the decimals their step needs', () => {
    expect(niceScale([12.3, 12.5]).digits).toBe(1);
    expect(niceScale([12.31, 12.38]).digits).toBe(2);
  });

  it('keeps a fixed range as given', () => {
    expect(niceScale([], { fixed: [0, 100], count: 2 })).toEqual({ min: 0, max: 100, ticks: [0, 50, 100], digits: 0 });
  });
});

describe('positions and paths', () => {
  const scale = { min: 0, max: 10, ticks: [], digits: 0 };
  it('puts the bottom of the scale at y 100 and the start of the move at x 0', () => {
    expect(yOf(scale, 0)).toBe(100);
    expect(yOf(scale, 10)).toBe(0);
    expect(xOf(2000, 500)).toBe(25);
  });

  it('breaks the line at a null (no reading is never drawn as 0)', () => {
    const points: DiagPoint[] = [
      [0, 1, 1, 12, 40],
      [500, null, null, 12, 50],
      [1000, 5, 5, 12, 60],
      [2000, 5, 5, 12, 60],
    ];
    expect(linePath(points, 1, scale)).toBe('M0.0 90.0 M50.0 50.0 L100.0 50.0');
  });

  it('finds the point nearest the pointer', () => {
    expect(nearestIndex(mockDiagMove.points, 0)).toBe(0);
    expect(nearestIndex(mockDiagMove.points, 0.3)).toBe(1);
    expect(nearestIndex(mockDiagMove.points, 2)).toBe(mockDiagMove.points.length - 1);
  });

  it('summarises the extremes, skipping nulls', () => {
    expect(summarise(mockDiagMove.points)).toMatchObject({ seconds: 2, ampsMax: 4.8, voltsMin: 12.01, dutyMax: 100 });
  });
});

describe('LiveBuffer', () => {
  it('records the move in progress once per new reading and starts again for the next move', () => {
    const buffer = new LiveBuffer();
    expect(buffer.add(reading(3, 0))).toBe(true);
    expect(buffer.add(reading(3, 0))).toBe(false); // the same reading sent again
    expect(buffer.add(reading(3, 250, true, null))).toBe(true);
    expect(buffer.points).toEqual([
      [0, 3, 3, 12.31, 100],
      [250, null, null, 12.31, 100],
    ]);
    expect(buffer.add(reading(3, 500, false))).toBe(false); // at rest: kept as the last move
    expect(buffer.points).toHaveLength(2);
    buffer.add(reading(4, 0));
    expect(buffer.moveId).toBe(4);
    expect(buffer.points).toHaveLength(1);
  });

  it('stays bounded over a long move and keeps its start and its latest reading', () => {
    const buffer = new LiveBuffer(100);
    for (let ms = 0; ms <= 180000; ms += 250) buffer.add(reading(9, ms));
    expect(buffer.points.length).toBeLessThanOrEqual(100);
    expect(buffer.points[0][0]).toBe(0);
    expect(buffer.points[buffer.points.length - 1][0]).toBe(180000);
  });
});
