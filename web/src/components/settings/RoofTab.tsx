import { ComponentChildren, FunctionalComponent } from 'preact';
import type { ParkConfig, RoofConfig, SafetyConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { DepToggle, Field, NumberInput, SelectInput, SettingsCard, TextInput, Toggle } from './controls';
import { Note } from '../ui';
import HallScale from '../../dashboard/HallScale';
import { t } from '../../i18n';

// Settings → Roof (spec 009 US4, FR-018 to FR-023): the roof's motion settings
// (spec 002), the weather sources (spec 004) and the park (specs 004, 005). The
// ranges are the device's (roofSchema.ts, configSchema.ts, the shared range
// fixture); the device refuses a save while the roof moves.

interface NumProps {
  at: string[]; // the config path
  label: string;
  hint?: ComponentChildren;
  unit?: string;
  integer?: boolean;
  value: number;
}

const Num: FunctionalComponent<NumProps & Pick<SettingsTabProps, 'update' | 'error'>> = ({
  at,
  label,
  hint,
  unit,
  integer,
  value,
  update,
  error,
}) => (
  <Field label={label} hint={hint} error={error(at.join('.'))}>
    <NumberInput dataField={at.join('.')} value={value} min={0} unit={unit} integer={integer} onChange={(v) => update(at, v)} />
  </Field>
);

type RoofProps = SettingsTabProps & { roof: RoofConfig };
const roofNum = (props: RoofProps, key: keyof RoofConfig, label: string, extra: Partial<NumProps> = {}) => (
  <Num {...extra} at={['roof', key]} label={label} value={props.roof[key] as number} update={props.update} error={props.error} />
);

const MotionCard: FunctionalComponent<RoofProps> = (props) => (
  <SettingsCard id="roof" title={t('settings.roof.motion')} hint={t('settings.roof.motionHint')}>
    <div class="form-grid">
      {roofNum(props, 'travelMm', t('settings.roof.travel'), { unit: 'mm', integer: true, hint: t('settings.roof.travelHint') })}
      {roofNum(props, 'speedMmPerSecond', t('settings.roof.speed'), { unit: 'mm/s', hint: t('settings.roof.speedHint') })}
      {roofNum(props, 'timeoutMarginSeconds', t('settings.roof.timeoutMargin'), {
        unit: 's',
        integer: true,
        hint: t('settings.roof.timeoutMarginHint'),
      })}
      {roofNum(props, 'leaveMagnetSeconds', t('settings.roof.leaveMagnet'), {
        unit: 's',
        integer: true,
        hint: t('settings.roof.leaveMagnetHint'),
      })}
      {roofNum(props, 'rampUpMs', t('settings.roof.rampUp'), { unit: 'ms', integer: true })}
      {roofNum(props, 'rampDownMs', t('settings.roof.rampDown'), { unit: 'ms', integer: true, hint: t('settings.roof.rampDownHint') })}
      {roofNum(props, 'topDutyPercent', t('settings.roof.topDuty'), { unit: '%', integer: true })}
      {roofNum(props, 'slowDutyPercent', t('settings.roof.slowDuty'), { unit: '%', integer: true, hint: t('settings.roof.slowDutyHint') })}
      <Field label={t('settings.roof.openDirection')} hint={t('settings.roof.openDirectionHint')}>
        <SelectInput
          dataField="roof.openDirection"
          value={props.roof.openDirection}
          options={[
            { value: 'forward', label: t('settings.roof.forward') },
            { value: 'reverse', label: t('settings.roof.reverse') },
          ]}
          onChange={(v) => props.update(['roof', 'openDirection'], v)}
        />
      </Field>
    </div>
  </SettingsCard>
);

// The live reading against the thresholds as edited, so calibration can be
// checked before saving (FR-020).
const HallCard: FunctionalComponent<RoofProps> = (props) => {
  const hall = props.status?.roof?.hall;
  return (
    <SettingsCard id="hall" title={t('settings.roof.hallSensor')} hint={t('settings.roof.hallHint')}>
      <div class="form-grid">
        <Field label={t('settings.roof.hallHighEnd')}>
          <SelectInput
            dataField="roof.hallHighEnd"
            value={props.roof.hallHighEnd}
            options={[
              { value: 'closed', label: t('settings.roof.endClosed') },
              { value: 'open', label: t('settings.roof.endOpen') },
            ]}
            onChange={(v) => props.update(['roof', 'hallHighEnd'], v)}
          />
        </Field>
        {roofNum(props, 'hallHighCounts', t('settings.roof.hallHigh'), { integer: true, hint: t('settings.roof.hallHighHint') })}
        {roofNum(props, 'hallLowCounts', t('settings.roof.hallLow'), { integer: true, hint: t('settings.roof.hallLowHint') })}
      </div>
      {hall ? (
        <div data-inventory="settings-hall-scale">
          <Note>{t('settings.roof.liveReading', { counts: hall.counts })}</Note>
          <HallScale counts={hall.counts} low={props.roof.hallLowCounts} high={props.roof.hallHighCounts} />
        </div>
      ) : (
        <Note>{t('settings.roof.noLiveReading')}</Note>
      )}
    </SettingsCard>
  );
};

// The current-sense values show while it is on, or while one of them needs fixing
// (it is still validated and saved while off).
const CURRENT_KEYS = ['maxCurrentAmps', 'currentAmpsPerVolt', 'minRunningAmps'] as const;
const showCurrent = (props: RoofProps) =>
  props.roof.currentSenseEnabled || CURRENT_KEYS.some((key) => props.error(`roof.${key}`) !== undefined);

const CurrentCard: FunctionalComponent<RoofProps> = (props) => (
  <SettingsCard id="current" title={t('settings.roof.currentAndSupply')}>
    <Toggle
      dataField="roof.currentSenseEnabled"
      label={t('settings.roof.currentSense')}
      hint={t('settings.roof.currentSenseHint')}
      checked={props.roof.currentSenseEnabled}
      onChange={(v) => props.update(['roof', 'currentSenseEnabled'], v)}
    />
    <div class="form-grid">
      {showCurrent(props) && roofNum(props, 'maxCurrentAmps', t('settings.roof.maxCurrent'), { unit: 'A' })}
      {showCurrent(props) &&
        roofNum(props, 'currentAmpsPerVolt', t('settings.roof.ampsPerVolt'), { unit: 'A/V', hint: t('settings.roof.ampsPerVoltHint') })}
      {showCurrent(props) &&
        roofNum(props, 'minRunningAmps', t('settings.roof.minRunning'), { unit: 'A', hint: t('settings.roof.minRunningHint') })}
      {roofNum(props, 'minSupplyVolts', t('settings.roof.minSupply'), { unit: 'V', hint: t('settings.roof.minSupplyHint') })}
    </div>
  </SettingsCard>
);

type SafetyProps = SettingsTabProps & { safety: SafetyConfig };

const SafetyCard: FunctionalComponent<SafetyProps> = ({ safety, update, error, deps, fix }) => (
  <SettingsCard id="safety" title={t('settings.roof.weatherSources')} hint={t('settings.roof.weatherSourcesHint')}>
    <Toggle
      dataField="safety.http.enabled"
      label={t('settings.roof.sqmeterHttp')}
      checked={safety.http.enabled}
      onChange={(v) => update(['safety', 'http', 'enabled'], v)}
    />
    {safety.http.enabled && (
      <div class="form-grid indent">
        <Field label={t('settings.roof.sqmeterUrl')} error={error('safety.http.url')}>
          <TextInput
            dataField="safety.http.url"
            type="url"
            value={safety.http.url}
            placeholder="http://sqmeter.local/api/safety"
            onInput={(v) => update(['safety', 'http', 'url'], v)}
          />
        </Field>
      </div>
    )}
    <DepToggle
      entry={deps.get('safety.mqtt.enabled')}
      onFix={fix}
      dataField="safety.mqtt.enabled"
      label={t('settings.roof.sqmeterMqtt')}
      hint={t('settings.roof.sqmeterMqttHint')}
      checked={safety.mqtt.enabled}
      onChange={(v) => update(['safety', 'mqtt', 'enabled'], v)}
    />
    {safety.mqtt.enabled && (
      <div class="form-grid indent">
        <Field label={t('settings.roof.sqmeterTopic')} error={error('safety.mqtt.topic')}>
          <TextInput dataField="safety.mqtt.topic" value={safety.mqtt.topic} onInput={(v) => update(['safety', 'mqtt', 'topic'], v)} />
        </Field>
      </div>
    )}
    <Toggle
      dataField="safety.rainContact"
      label={t('settings.roof.rainContact')}
      hint={t('settings.roof.rainContactHint')}
      checked={safety.rainContact}
      onChange={(v) => update(['safety', 'rainContact'], v)}
    />
    <div class="form-grid">
      <Num
        at={['safety', 'staleSeconds']}
        label={t('settings.roof.staleAfter')}
        hint={t('settings.roof.staleAfterHint')}
        unit="s"
        integer
        value={safety.staleSeconds}
        update={update}
        error={error}
      />
    </div>
  </SettingsCard>
);

type ParkProps = SettingsTabProps & { park: ParkConfig };

const ParkCard: FunctionalComponent<ParkProps> = ({ park, update, error }) => (
  <SettingsCard id="park" title={t('settings.roof.park')} hint={t('settings.roof.parkHint')}>
    <div class="form-grid">
      <Field label={t('settings.roof.ninaUrl')} hint={t('settings.roof.ninaUrlHint')} error={error('park.nina.url')}>
        <TextInput
          dataField="park.nina.url"
          type="url"
          value={park.nina.url}
          placeholder="http://192.168.1.20:1888"
          onInput={(v) => update(['park', 'nina', 'url'], v)}
        />
      </Field>
      <Field label={t('settings.roof.parkSwitches')} hint={t('settings.roof.parkSwitchesHint')}>
        <SelectInput
          dataField="park.switches"
          value={park.switches}
          options={[
            { value: 'none', label: t('settings.roof.switchesNone') },
            { value: 'one', label: t('settings.roof.switchesOne') },
            { value: 'two', label: t('settings.roof.switchesTwo') },
          ]}
          onChange={(v) => update(['park', 'switches'], v)}
        />
      </Field>
      <Num
        at={['park', 'timeoutSeconds']}
        label={t('settings.roof.parkTimeLimit')}
        hint={t('settings.roof.parkTimeLimitHint')}
        unit="s"
        integer
        value={park.timeoutSeconds}
        update={update}
        error={error}
      />
    </div>
  </SettingsCard>
);

// The device refuses every save while the roof moves; a save while it is
// stopped part-way makes its position unknown (FR-021).
const MotionNote: FunctionalComponent<Pick<SettingsTabProps, 'status'>> = ({ status }) => {
  const state = status?.roof?.state;
  if (state === 'opening' || state === 'closing') return <Note tone="warn">{t('settings.roof.movingNote')}</Note>;
  if (state === 'stopped') return <Note tone="warn">{t('settings.roof.stoppedNote')}</Note>;
  return null;
};

const RoofTab: FunctionalComponent<SettingsTabProps> = (props) => {
  const { roof, safety, park } = props.config;
  return (
    <>
      <MotionNote status={props.status} />
      {roof ? (
        <>
          <MotionCard {...props} roof={roof} />
          <HallCard {...props} roof={roof} />
          <CurrentCard {...props} roof={roof} />
        </>
      ) : (
        <Note>{t('settings.roof.notSent')}</Note>
      )}
      {safety && <SafetyCard {...props} safety={safety} />}
      {park && <ParkCard {...props} park={park} />}
    </>
  );
};

export default RoofTab;
