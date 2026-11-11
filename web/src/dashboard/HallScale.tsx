import { FunctionalComponent } from 'preact';
import { hallMarks } from '../lib/roofView';
import { t } from '../i18n';
import { formatCount } from '../i18n/format';

// The live Hall reading on its 0-4095 scale with the low and high thresholds
// marked (spec 009 FR-013, FR-020): what calibration looks at. The thresholds
// are the saved ones on the dashboard and the edited ones in Settings.

const pct = (value: number) => `${(value * 100).toFixed(2)}%`;

export const hallScaleLabel = (counts: number, low?: number, high?: number) =>
  typeof low === 'number' && typeof high === 'number'
    ? t('roof.hall.scaleLabel', { counts: formatCount(counts), low: formatCount(low), high: formatCount(high) })
    : t('roof.hall.scaleLabelNoThresholds', { counts: formatCount(counts) });

const HallScale: FunctionalComponent<{ counts: number; low?: number; high?: number }> = ({ counts, low, high }) => {
  const marks = hallMarks(counts, low, high);
  return (
    <svg class="hall-scale" role="img" aria-label={hallScaleLabel(counts, low, high)} viewBox="0 0 100 14" preserveAspectRatio="none">
      <rect class="hall-scale-track" x="0" y="5" width="100" height="4" rx="2" />
      {marks.low !== null && <line class="hall-scale-threshold" x1={pct(marks.low)} x2={pct(marks.low)} y1="1" y2="13" />}
      {marks.high !== null && <line class="hall-scale-threshold" x1={pct(marks.high)} x2={pct(marks.high)} y1="1" y2="13" />}
      <line class="hall-scale-reading" x1={pct(marks.reading)} x2={pct(marks.reading)} y1="0" y2="14" />
    </svg>
  );
};

export default HallScale;
