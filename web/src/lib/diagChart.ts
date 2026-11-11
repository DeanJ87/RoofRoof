import type { DiagLive, DiagPoint } from '../types/diag';
import { svgNumber } from './svg';

// The Diagnostics page's chart (spec 017 FR-032, FR-034): scales, paths and the
// live buffer, kept free of the DOM so they can be tested. Plots are drawn in a
// 0-100 box on both axes; the SVG stretches it to the panel.

/** The live chart keeps at most this many points; a longer move halves its resolution. */
export const LIVE_MAX_POINTS = 1200;

// Columns of a point, as in GET /api/diag/moves/{id}.
export const T = 0;
export const AMPS_AVG = 1;
export const AMPS_MAX = 2;
export const VOLTS = 3;
export const DUTY = 4;

export interface Scale {
  min: number;
  max: number;
  ticks: number[];
  digits: number; // decimals the ticks need
}

const finite = (value: number | null | undefined): value is number => typeof value === 'number' && Number.isFinite(value);

/** A step of 1, 2 or 5 times a power of ten that splits `span` into about `count` parts. */
const niceStep = (span: number, count: number) => {
  const raw = span / count;
  const power = 10 ** Math.floor(Math.log10(raw));
  const unit = raw / power;
  return (unit <= 1 ? 1 : unit <= 2 ? 2 : unit <= 5 ? 5 : 10) * power;
};

/**
 * A scale for `values` (nulls ignored) with round ticks. `floor` keeps zero in
 * view (current, duty); `fixed` sets the range outright.
 */
export const niceScale = (values: (number | null)[], options: { floor?: number; fixed?: [number, number]; count?: number } = {}): Scale => {
  const count = options.count ?? 3;
  const known = values.filter(finite);
  let [low, high] = options.fixed ?? [Math.min(...known), Math.max(...known)];
  if (!options.fixed && known.length === 0) [low, high] = [0, 1];
  if (options.floor !== undefined) low = Math.min(low, options.floor);
  if (high - low < 1e-6) [low, high] = [low - 0.5, high + 0.5];
  const step = niceStep(high - low, count);
  const min = options.fixed ? low : Math.floor(low / step) * step;
  const max = options.fixed ? high : Math.ceil(high / step) * step;
  const ticks: number[] = [];
  for (let tick = min; tick <= max + step / 1e6; tick += step) ticks.push(Math.round(tick * 1e9) / 1e9);
  return { min, max, ticks, digits: Math.max(0, -Math.floor(Math.log10(step) + 1e-9)) };
};

/** Where `value` sits on a scale: 0 at the bottom, 100 at the top (SVG y is the other way). */
export const yOf = (scale: Pick<Scale, 'min' | 'max'>, value: number) => 100 - ((value - scale.min) / (scale.max - scale.min || 1)) * 100;

/** Where time `tMs` sits, 0-100, across a move of `spanMs`. */
export const xOf = (spanMs: number, tMs: number) => (spanMs > 0 ? (tMs / spanMs) * 100 : 0);

/** The x axis's span: the last point's time, at least one second. */
export const spanOf = (points: DiagPoint[]) => Math.max(1000, points.length ? points[points.length - 1][T] : 0);

/** An SVG path through one column; a null breaks the line (no reading, never 0). */
export const linePath = (points: DiagPoint[], column: number, scale: Scale, spanMs = spanOf(points)) => {
  let path = '';
  let drawing = false;
  for (const point of points) {
    const value = point[column];
    if (!finite(value)) {
      drawing = false;
      continue;
    }
    path += `${path ? ' ' : ''}${drawing ? 'L' : 'M'}${svgNumber(xOf(spanMs, point[T]))} ${svgNumber(yOf(scale, value))}`;
    drawing = true;
  }
  return path;
};

/** The index of the point nearest `fraction` (0-1) of the way across. */
export const nearestIndex = (points: DiagPoint[], fraction: number, spanMs = spanOf(points)) => {
  const target = Math.max(0, Math.min(1, fraction)) * spanMs;
  let best = 0;
  points.forEach((point, index) => {
    if (Math.abs(point[T] - target) < Math.abs(points[best][T] - target)) best = index;
  });
  return best;
};

/** What a screen reader hears: the move's length and each series' extremes. */
export interface ChartSummary {
  seconds: number;
  ampsAvg: number | null;
  ampsMax: number | null;
  voltsMin: number | null;
  dutyMax: number | null;
}

const extreme = (points: DiagPoint[], column: number, pick: (...values: number[]) => number) => {
  const values = points.map((point) => point[column]).filter(finite);
  return values.length ? pick(...values) : null;
};

export const summarise = (points: DiagPoint[]): ChartSummary => {
  const amps = points.map((point) => point[AMPS_AVG]).filter(finite);
  return {
    seconds: points.length ? points[points.length - 1][T] / 1000 : 0,
    ampsAvg: amps.length ? amps.reduce((sum, value) => sum + value, 0) / amps.length : null,
    ampsMax: extreme(points, AMPS_MAX, Math.max),
    voltsMin: extreme(points, VOLTS, Math.min),
    dutyMax: extreme(points, DUTY, Math.max),
  };
};

/**
 * The move in progress, from the stream: one point per new reading, reset
 * when another move starts, halved in resolution when it reaches `max` so a
 * long move still fits whole.
 */
export class LiveBuffer {
  moveId: number | null = null;
  points: DiagPoint[] = [];

  constructor(private readonly max = LIVE_MAX_POINTS) {}

  /** Adds the document's reading; true when the buffer changed. */
  add(live: DiagLive): boolean {
    const move = live.move;
    const elapsed = live.roof.elapsedMs;
    if (!move?.active || !finite(elapsed)) return false;
    if (move.id !== this.moveId) {
      this.moveId = move.id;
      this.points = [];
    }
    const last = this.points[this.points.length - 1];
    if (last && elapsed <= last[T]) return false; // the same reading again
    const amps = live.roof.motorAmps;
    this.points.push([elapsed, amps, amps, live.roof.supplyVolts, live.roof.dutyPercent]);
    if (this.points.length > this.max) {
      const end = this.points.length - 1;
      this.points = this.points.filter((_, index) => index % 2 === 0 || index === end);
    }
    return true;
  }
}
