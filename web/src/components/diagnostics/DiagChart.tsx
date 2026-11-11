import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { DiagPoint } from '../../types/diag';
import {
  AMPS_AVG,
  AMPS_MAX,
  DUTY,
  T,
  VOLTS,
  linePath,
  nearestIndex,
  niceScale,
  spanOf,
  summarise,
  xOf,
  yOf,
  type Scale,
} from '../../lib/diagChart';
import { svgNumber } from '../../lib/svg';
import { formatNumber } from '../../i18n/format';
import { t } from '../../i18n';

// Current, supply and duty through one move as small multiples sharing the
// time axis (spec 017 FR-034): one measure and unit per plot, round ticks, a
// crosshair that follows the pointer or the arrow keys, and the extremes in
// words for screen readers.

interface Series {
  column: number;
  label: string;
  className: string;
}

interface Panel {
  title: string;
  unit: string;
  scale: Scale;
  series: Series[];
}

const pct = (value: number) => `${svgNumber(value)}%`;

const Plot: FunctionalComponent<{ panel: Panel; points: DiagPoint[]; spanMs: number; cursor: number | null }> = ({
  panel,
  points,
  spanMs,
  cursor,
}) => (
  <div class="diag-panel">
    <div class="diag-panel-head">
      <span class="diag-panel-title">
        {panel.title} <span class="ltr">({panel.unit})</span>
      </span>
      {panel.series.length > 1 && (
        <span class="diag-legend">
          {panel.series.map((series) => (
            <span key={series.label}>
              <i class={`diag-key ${series.className}`} />
              {series.label}
            </span>
          ))}
        </span>
      )}
    </div>
    <div class="diag-plot ltr">
      {panel.scale.ticks.map((tick) => (
        <span key={tick} class="diag-tick-y" style={{ top: pct(yOf(panel.scale, tick)) }}>
          {formatNumber(tick, panel.scale.digits)}
        </span>
      ))}
      <svg viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
        {panel.scale.ticks.map((tick) => (
          <line
            key={tick}
            class="diag-grid"
            x1="0"
            x2="100"
            y1={svgNumber(yOf(panel.scale, tick))}
            y2={svgNumber(yOf(panel.scale, tick))}
          />
        ))}
        {panel.series.map((series) => (
          <path key={series.label} class={series.className} d={linePath(points, series.column, panel.scale, spanMs)} />
        ))}
        {cursor !== null && (
          <line
            class="diag-cursor"
            x1={svgNumber(xOf(spanMs, points[cursor][T]))}
            x2={svgNumber(xOf(spanMs, points[cursor][T]))}
            y1="0"
            y2="100"
          />
        )}
      </svg>
    </div>
  </div>
);

export const chartSummary = (points: DiagPoint[], currentSense: boolean) => {
  if (points.length < 2) return t('diagnostics.chart.noData');
  const s = summarise(points);
  return t(currentSense ? 'diagnostics.chart.summary' : 'diagnostics.chart.summaryNoCurrent', {
    seconds: formatNumber(s.seconds, 1),
    average: formatNumber(s.ampsAvg, 2),
    peak: formatNumber(s.ampsMax, 2),
    volts: formatNumber(s.voltsMin, 2),
    duty: formatNumber(s.dutyMax, 0),
  });
};

const DiagChart: FunctionalComponent<{ points: DiagPoint[]; currentSense: boolean }> = ({ points, currentSense }) => {
  const [hover, setHover] = useState<number | null>(null);
  const summary = chartSummary(points, currentSense);
  if (points.length < 2)
    return (
      <p class="note note-muted" role="img" aria-label={summary}>
        {summary}
      </p>
    );
  const spanMs = spanOf(points);
  const column = (index: number) => points.map((point) => point[index]);
  const panels: Panel[] = [
    {
      title: t('diagnostics.chart.supply'),
      unit: 'V',
      scale: niceScale(column(VOLTS)),
      series: [{ column: VOLTS, label: t('diagnostics.chart.minimum'), className: 'diag-line' }],
    },
    {
      title: t('diagnostics.chart.duty'),
      unit: '%',
      scale: niceScale([], { fixed: [0, 100], count: 2 }),
      series: [{ column: DUTY, label: t('diagnostics.chart.duty'), className: 'diag-line' }],
    },
  ];
  if (currentSense)
    panels.unshift({
      title: t('diagnostics.chart.current'),
      unit: 'A',
      scale: niceScale([...column(AMPS_AVG), ...column(AMPS_MAX)], { floor: 0 }),
      series: [
        { column: AMPS_AVG, label: t('diagnostics.chart.average'), className: 'diag-line' },
        { column: AMPS_MAX, label: t('diagnostics.chart.peak'), className: 'diag-line-max' },
      ],
    });
  // The readout shows the crosshair's point, else the latest.
  const cursor = hover ?? points.length - 1;
  const at = points[cursor];
  const move = (event: PointerEvent) => {
    const box = (event.currentTarget as HTMLElement).querySelector('.diag-plot')?.getBoundingClientRect();
    if (box && box.width > 0) setHover(nearestIndex(points, (event.clientX - box.left) / box.width, spanMs));
  };
  const key = (event: KeyboardEvent) => {
    const step = { ArrowLeft: -1, ArrowRight: 1, Home: -points.length, End: points.length }[event.key];
    if (step === undefined) return;
    event.preventDefault();
    setHover(Math.max(0, Math.min(points.length - 1, cursor + step)));
  };
  const reading = (label: string, value: string) => (
    <span>
      {label} <strong class="ltr">{value}</strong>
    </span>
  );
  return (
    <div class="diag-chart">
      {/* Arrow keys move the crosshair; the readout below gives its values. */}
      <div
        class="diag-plots"
        role="img"
        aria-label={summary}
        tabIndex={0}
        onPointerMove={move}
        onPointerLeave={() => setHover(null)}
        onKeyDown={key}
      >
        {panels.map((panel) => (
          <Plot key={panel.title} panel={panel} points={points} spanMs={spanMs} cursor={hover} />
        ))}
        <div class="diag-plot diag-axis-x ltr" aria-hidden="true">
          {niceScale([0, spanMs / 1000], { floor: 0 }).ticks.map((tick) =>
            tick * 1000 <= spanMs ? (
              <span key={tick} style={{ left: pct(xOf(spanMs, tick * 1000)) }}>
                {formatNumber(tick, 0)}
              </span>
            ) : null,
          )}
        </div>
        <div class="diag-axis-title" aria-hidden="true">
          {t('diagnostics.chart.time')} <span class="ltr">(s)</span>
        </div>
      </div>
      <p class="diag-readout">
        {reading(t('diagnostics.chart.time'), `${formatNumber(at[T] / 1000, 1)} s`)}
        {currentSense && reading(t('diagnostics.chart.average'), `${formatNumber(at[AMPS_AVG], 2)} A`)}
        {currentSense && reading(t('diagnostics.chart.peak'), `${formatNumber(at[AMPS_MAX], 2)} A`)}
        {reading(t('diagnostics.chart.supply'), `${formatNumber(at[VOLTS], 2)} V`)}
        {reading(t('diagnostics.chart.duty'), `${formatNumber(at[DUTY], 0)} %`)}
      </p>
    </div>
  );
};

export default DiagChart;
