import { ComponentChildren, FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import type { DaylightConfig, LocationConfig, ParkConfig, RoofConfig, SafetyConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { DepNote, DepToggle, Field, Fold, NumberInput, SelectInput, SettingsCard, TextInput, Toggle } from './controls';
import AutoCloseCard from './AutoCloseCard';
import { Button, Note } from '../ui';
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

export type RoofProps = SettingsTabProps & { roof: RoofConfig };
export const roofNum = (props: RoofProps, key: keyof RoofConfig, label: string, extra: Partial<NumProps> = {}) => (
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
    <Toggle
      dataField="ui.confirmOpenClose"
      label={t('settings.roof.confirmOpenClose')}
      hint={t('settings.roof.confirmOpenCloseHint')}
      checked={props.config.ui?.confirmOpenClose ?? false}
      onChange={(v) => props.update(['ui', 'confirmOpenClose'], v)}
    />
  </SettingsCard>
);

// The live reading against the thresholds as edited, so calibration can be
// checked before saving (FR-020).
export const HallCard: FunctionalComponent<RoofProps> = (props) => {
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

export const CurrentCard: FunctionalComponent<RoofProps> = (props) => (
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

export type SafetyProps = SettingsTabProps & { safety: SafetyConfig };

// SQMeter over HTTP and MQTT (also the setup wizard's weather page, spec 015).
export const WeatherSourceFields: FunctionalComponent<SafetyProps> = ({ safety, update, error, deps, fix }) => (
  <>
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
  </>
);

// The rain contact on GPIO4 (also the setup wizard's hardware page).
export const RainContactToggle: FunctionalComponent<SafetyProps> = ({ safety, update }) => (
  <Toggle
    dataField="safety.rainContact"
    label={t('settings.roof.rainContact')}
    hint={t('settings.roof.rainContactHint')}
    checked={safety.rainContact}
    onChange={(v) => update(['safety', 'rainContact'], v)}
  />
);

const SafetyCard: FunctionalComponent<SafetyProps> = (props) => (
  <SettingsCard id="safety" title={t('settings.roof.weatherSources')} hint={t('settings.roof.weatherSourcesHint')}>
    <WeatherSourceFields {...props} />
    <RainContactToggle {...props} />
    <div class="form-grid">
      <Num
        at={['safety', 'staleSeconds']}
        label={t('settings.roof.staleAfter')}
        hint={t('settings.roof.staleAfterHint')}
        unit="s"
        integer
        value={props.safety.staleSeconds}
        update={props.update}
        error={props.error}
      />
    </div>
  </SettingsCard>
);

export type ParkProps = SettingsTabProps & { park: ParkConfig };

// N.I.N.A.'s address and the park switches (also the setup wizard's pages, spec 015).
export const NinaUrlField: FunctionalComponent<ParkProps> = ({ park, update, error }) => (
  <Field label={t('settings.roof.ninaUrl')} hint={t('settings.roof.ninaUrlHint')} error={error('park.nina.url')}>
    <TextInput
      dataField="park.nina.url"
      type="url"
      value={park.nina.url}
      placeholder="http://192.168.1.20:1888"
      onInput={(v) => update(['park', 'nina', 'url'], v)}
    />
  </Field>
);

export const ParkSwitchesField: FunctionalComponent<ParkProps> = ({ park, update }) => (
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
);

const ParkCard: FunctionalComponent<ParkProps> = (props) => (
  <SettingsCard id="park" title={t('settings.roof.park')} hint={t('settings.roof.parkHint')}>
    <div class="form-grid">
      <NinaUrlField {...props} />
      <ParkSwitchesField {...props} />
      <Num
        at={['park', 'timeoutSeconds']}
        label={t('settings.roof.parkTimeLimit')}
        hint={t('settings.roof.parkTimeLimitHint')}
        unit="s"
        integer
        value={props.park.timeoutSeconds}
        update={props.update}
        error={props.error}
      />
    </div>
  </SettingsCard>
);

export type DaylightProps = SettingsTabProps & { daylight: DaylightConfig; location: LocationConfig };

// A degrees field that can be negative (the sun's altitude, latitude, longitude).
const DegreesField: FunctionalComponent<{
  at: string[];
  label: string;
  hint: string;
  value: number;
  update: SettingsTabProps['update'];
  error: SettingsTabProps['error'];
}> = ({ at, label, hint, value, update, error }) => (
  <Field label={label} hint={hint} error={error(at.join('.'))}>
    <NumberInput dataField={at.join('.')} value={value} unit="°" onChange={(v) => update(at, v)} />
  </Field>
);

// The daylight failsafe and where the roof is (spec 013). The site and the
// threshold only matter while the check is on; the fields stay editable.
// The mode and the threshold (also the setup wizard's daylight page, spec 015).
export const DaylightModeFields: FunctionalComponent<Omit<DaylightProps, 'location'>> = ({ daylight, update, error, deps, fix }) => (
  <>
    <div class="form-grid">
      <Field label={t('settings.roof.daylightCheck')} hint={t('settings.roof.daylightCheckHint')} error={error('daylight.mode')}>
        <SelectInput
          dataField="daylight.mode"
          value={daylight.mode}
          options={[
            { value: 'off', label: t('settings.roof.daylightOff') },
            { value: 'alert', label: t('settings.roof.daylightAlert') },
            { value: 'close', label: t('settings.roof.daylightClose') },
          ]}
          onChange={(v) => update(['daylight', 'mode'], v)}
        />
      </Field>
      <DegreesField
        at={['daylight', 'sunAltitudeDeg']}
        label={t('settings.roof.sunAltitudeLimit')}
        hint={t('settings.roof.sunAltitudeLimitHint')}
        value={daylight.sunAltitudeDeg}
        update={update}
        error={error}
      />
    </div>
    <DepNote entry={deps.get('daylight.mode')} onFix={fix} />
    <DepNote entry={deps.get('daylight.sunAltitudeDeg')} onFix={fix} />
  </>
);

// Where the roof is (also the setup wizard's site page).
export const LocationFields: FunctionalComponent<Pick<DaylightProps, 'location' | 'update' | 'error'>> = ({ location, update, error }) => (
  <>
    <Toggle
      dataField="location.set"
      label={t('settings.roof.locationSet')}
      hint={t('settings.roof.locationSetHint')}
      checked={location.set}
      onChange={(v) => update(['location', 'set'], v)}
    />
    {(location.set || error('location.latitude') !== undefined || error('location.longitude') !== undefined) && (
      <div class="form-grid indent">
        <DegreesField
          at={['location', 'latitude']}
          label={t('settings.roof.latitude')}
          hint={t('settings.roof.latitudeHint')}
          value={location.latitude}
          update={update}
          error={error}
        />
        <DegreesField
          at={['location', 'longitude']}
          label={t('settings.roof.longitude')}
          hint={t('settings.roof.longitudeHint')}
          value={location.longitude}
          update={update}
          error={error}
        />
      </div>
    )}
  </>
);

const DaylightCard: FunctionalComponent<DaylightProps> = (props) => (
  <SettingsCard id="daylight" title={t('settings.roof.daylight')} hint={t('settings.roof.daylightHint')}>
    <DaylightModeFields {...props} />
    <LocationFields {...props} />
    {/* Spec 014: Settings only, not the setup wizard's daylight page. */}
    <Fold label={t('settings.roof.advanced')} open={props.error('daylight.bandDeg') !== undefined} dataField="daylight.advanced">
      <div class="form-grid">
        <Field label={t('settings.roof.daylightBand')} hint={t('settings.roof.daylightBandHint')} error={props.error('daylight.bandDeg')}>
          <NumberInput
            dataField="daylight.bandDeg"
            value={props.daylight.bandDeg ?? 0.5}
            min={0}
            unit="°"
            onChange={(v) => props.update(['daylight', 'bandDeg'], v)}
          />
        </Field>
      </div>
    </Fold>
  </SettingsCard>
);

// Spec 016: the attended calibration run, on its own page; its time limit under Advanced.
export const CalibrateCard: FunctionalComponent<RoofProps> = (props) => (
  <SettingsCard id="calibrate" title={t('calibrate.cardTitle')} hint={t('calibrate.cardHint')}>
    <Note>{t('calibrate.cardText')}</Note>
    <div class="btn-row">
      <Button onClick={() => route('/calibrate')}>{t('calibrate.open')}</Button>
    </div>
    <Fold label={t('settings.roof.advanced')} open={props.error('roof.calibrationLimitSeconds') !== undefined} dataField="calibrate.advanced">
      <div class="form-grid">
        {roofNum(props, 'calibrationLimitSeconds', t('calibrate.limit'), { unit: 's', integer: true, hint: t('calibrate.limitHint') })}
      </div>
    </Fold>
  </SettingsCard>
);

// The device refuses every save while the roof moves; a save while it is
// stopped part-way makes its position unknown (FR-021).
export const MotionNote: FunctionalComponent<Pick<SettingsTabProps, 'status'>> = ({ status }) => {
  const state = status?.roof?.state;
  if (state === 'opening' || state === 'closing') return <Note tone="warn">{t('settings.roof.movingNote')}</Note>;
  if (state === 'stopped') return <Note tone="warn">{t('settings.roof.stoppedNote')}</Note>;
  return null;
};

const RoofTab: FunctionalComponent<SettingsTabProps> = (props) => {
  const { roof, safety, park, daylight, location, autoClose, mqtt } = props.config;
  return (
    <>
      <MotionNote status={props.status} />
      {roof ? (
        <>
          <MotionCard {...props} roof={roof} />
          <HallCard {...props} roof={roof} />
          <CalibrateCard {...props} roof={roof} />
          <CurrentCard {...props} roof={roof} />
        </>
      ) : (
        <Note>{t('settings.roof.notSent')}</Note>
      )}
      {safety && <SafetyCard {...props} safety={safety} />}
      {park && <ParkCard {...props} park={park} />}
      {autoClose && <AutoCloseCard {...props} autoClose={autoClose} mqtt={mqtt} />}
      {daylight && location && <DaylightCard {...props} daylight={daylight} location={location} />}
    </>
  );
};

export default RoofTab;
