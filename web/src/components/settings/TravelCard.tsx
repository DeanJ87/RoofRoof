import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { Fold, SettingsCard } from './controls';
import { InfoTip, Note, ReadingRow } from '../ui';
import { roofNum, type RoofProps } from './RoofTab';
import { moveLimits } from '../../lib/moveLimits';
import { MEASURED, motionDefaults, valueSource } from '../../lib/setupProgress';
import { formatNumber } from '../../i18n/format';
import { t } from '../../i18n';

const SOURCE_NOTE = {
  measured: 'settings.roof.source.measured',
  default: 'settings.roof.source.default',
  typed: 'settings.roof.source.typed',
} as const;

/**
 * Spec 026: travel, speed and the margin, and the move time limits they make (as the device works
 * them out), who set them, and the calibration run that measures them (its own limit under Advanced).
 */
const TravelCard: FunctionalComponent<RoofProps> = (props) => {
  const limits = moveLimits(props.roof);
  const source = valueSource(props.roof, MEASURED.travel | MEASURED.speed, motionDefaults(props.roof));
  const calibrate = { label: t('calibrate.title'), onClick: () => route('/calibrate') };
  return (
    <SettingsCard id="travel" title={t('settings.roof.travelCard')} hint={t('settings.roof.motionHint')}>
      <div class="form-grid">
        {roofNum(props, 'travelMm', t('settings.roof.travel'), { unit: 'mm', integer: true, hint: t('settings.roof.travelHint') })}
        {roofNum(props, 'speedMmPerSecond', t('settings.roof.speed'), { unit: 'mm/s', hint: t('settings.roof.speedHint') })}
        {roofNum(props, 'slowSpeedMmPerSecond', t('settings.roof.slowSpeed'), { unit: 'mm/s', hint: t('settings.roof.slowSpeedHint') })}
        {roofNum(props, 'timeoutMarginSeconds', t('settings.roof.timeoutMargin'), {
          unit: 's',
          integer: true,
          hint: t('settings.roof.timeoutMarginHint'),
        })}
      </div>
      <Note
        tone={source === 'default' ? 'warn' : source === 'measured' ? 'ok' : 'muted'}
        action={source === 'measured' ? undefined : calibrate}
      >
        {t(SOURCE_NOTE[source])}
      </Note>
      <div data-move-limits>
        <div class="reading-row">
          <span class="reading-label">
            {t('settings.roof.moveLimit')} <InfoTip text={t('settings.roof.moveLimitHint')} />
          </span>
          <strong class="reading-value">{t('settings.roof.seconds', { value: formatNumber(limits.fullSeconds, 1) })}</strong>
        </div>
        <ReadingRow
          label={t('settings.roof.moveLimitUnknown')}
          value={t('settings.roof.seconds', { value: formatNumber(limits.unknownSeconds, 1) })}
        />
      </div>
      <Fold
        label={t('settings.roof.advanced')}
        open={props.error('roof.calibrationLimitSeconds') !== undefined}
        dataField="calibrate.advanced"
      >
        <div class="form-grid">
          {roofNum(props, 'calibrationLimitSeconds', t('calibrate.limit'), { unit: 's', integer: true, hint: t('calibrate.limitHint') })}
        </div>
      </Fold>
    </SettingsCard>
  );
};

export default TravelCard;
