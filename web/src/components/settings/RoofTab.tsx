import { ComponentChildren, FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import type { DaylightConfig, LocationConfig, RoofConfig, SafetyConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { DepNote, DepToggle, Field, Fold, NumberInput, SelectInput, SettingsCard, TextInput, Toggle } from './controls';
import AutoCloseCard from './AutoCloseCard';
import { Button, Note, Pill, ReadingRow } from '../ui';
import { setupStages } from '../../lib/setupProgress';
import { SetupStages, stagesDone } from '../SetupProgress';
import TravelCard from './TravelCard';
import { presetLabel } from '../../lib/pinsView';
import TestBoxCard, { RequestParkToggle } from './TestBoxCard';
import SqmeterCopy from './SqmeterCopy';
import { SqmeterLoginFields } from './SqmeterLoginFields';
import { SqmeterAddressTest } from './SqmeterAddressTest';
import { sqmeterHttps } from '../../lib/sqmeterAddress';
import { FollowsSqmeter } from './FollowsSqmeter';
import { fromCopy, synced } from '../../lib/syncView';
import { DegreesField, LocationFields } from './LocationFields';
import { NinaUrlField, ParkNotes, ParkSourceField, ParkSwitchesField, type ParkProps } from './ParkFields';
export { NinaUrlField, ParkNotes, ParkSourceField, ParkSwitchesField, type ParkProps };
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

// Spec 026: the motor's direction, duties and ramps (set before the first move).
const MotionCard: FunctionalComponent<RoofProps> = (props) => (
  <SettingsCard id="roof" title={t('settings.roof.motion')} hint={t('settings.roof.motionHint')}>
    <div class="form-grid">
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
      {roofNum(props, 'topDutyPercent', t('settings.roof.topDuty'), { unit: '%', integer: true })}
      {roofNum(props, 'slowDutyPercent', t('settings.roof.slowDuty'), { unit: '%', integer: true, hint: t('settings.roof.slowDutyHint') })}
      {roofNum(props, 'rampUpMs', t('settings.roof.rampUp'), { unit: 'ms', integer: true })}
      {roofNum(props, 'rampDownMs', t('settings.roof.rampDown'), { unit: 'ms', integer: true, hint: t('settings.roof.rampDownHint') })}
      {roofNum(props, 'leaveMagnetSeconds', t('settings.roof.leaveMagnet'), {
        unit: 's',
        integer: true,
        hint: t('settings.roof.leaveMagnetHint'),
      })}
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
// The fields and the live scale (also the setup wizard's Hall screen, spec 018).
export const HallFields: FunctionalComponent<RoofProps> = (props) => {
  const hall = props.status?.roof?.hall;
  return (
    <>
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
    </>
  );
};

export const HallCard: FunctionalComponent<RoofProps> = (props) => (
  <SettingsCard id="hall" title={t('settings.roof.hallSensor')} hint={t('settings.roof.hallHint')}>
    <HallFields {...props} />
  </SettingsCard>
);

// The current-sense values show while it is on, or while one of them needs fixing
// (it is still validated and saved while off).
const CURRENT_KEYS = ['maxCurrentAmps', 'currentAmpsPerVolt', 'minRunningAmps'] as const;
const showCurrent = (props: RoofProps) =>
  props.roof.currentSenseEnabled || CURRENT_KEYS.some((key) => props.error(`roof.${key}`) !== undefined);

// The current-sense scaling (also the setup wizard's current-sense screen, spec 018).
export const CurrentFields: FunctionalComponent<RoofProps> = (props) =>
  showCurrent(props) ? (
    <>
      {roofNum(props, 'maxCurrentAmps', t('settings.roof.maxCurrent'), { unit: 'A' })}
      {roofNum(props, 'currentAmpsPerVolt', t('settings.roof.ampsPerVolt'), { unit: 'A/V', hint: t('settings.roof.ampsPerVoltHint') })}
      {roofNum(props, 'minRunningAmps', t('settings.roof.minRunning'), { unit: 'A', hint: t('settings.roof.minRunningHint') })}
    </>
  ) : null;

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
      <CurrentFields {...props} />
      {roofNum(props, 'minSupplyVolts', t('settings.roof.minSupply'), { unit: 'V', hint: t('settings.roof.minSupplyHint') })}
    </div>
  </SettingsCard>
);

export type SafetyProps = SettingsTabProps & { safety: SafetyConfig };

// SQMeter over HTTP and MQTT.
export const WeatherSourceFields: FunctionalComponent<SafetyProps> = ({ safety, update, error, deps, fix, config, status }) => (
  <>
    <Toggle
      dataField="safety.http.enabled"
      label={t('settings.roof.sqmeterHttp')}
      checked={safety.http.enabled}
      onChange={(v) => update(['safety', 'http', 'enabled'], v)}
    />
    {safety.http.enabled && (
      <div class="form-grid indent">
        <SqmeterUrlField safety={safety} update={update} error={error} />
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
    {/* Spec 026: no source at all reads unsafe and starts the automatic close; a test box bypasses it instead. */}
    {!safety.http.enabled && !safety.mqtt.enabled && !config.testBox?.weather && (
      <Note tone="warn" action={{ label: t('testBox.title'), onClick: () => route('/test-box') }}>
        {t('settings.roof.noWeatherSource')}
      </Note>
    )}
    {/* Spec 020: also while off when SQMeter's topic came in (a copy, or the MQTT group synced). */}
    {(safety.mqtt.enabled || fromCopy('weatherTopic') || synced(config, 'mqtt')) && (
      <div class="form-grid indent">
        <SqmeterTopicField safety={safety} update={update} error={error} config={config} status={status} />
      </div>
    )}
  </>
);

// SQMeter's address and topic (also the setup wizard's SQMeter screen, spec 025).
type SourceFieldProps = Pick<SafetyProps, 'safety' | 'update' | 'error'>;
// With SQMeter's login (spec 020): the address and the login go together everywhere.
// The address only (FR-036): RoofRoof adds /api/safety itself; https is encrypted, its
// certificate not checked (FR-039). `test`: the Test button (FR-038), except where a
// page has its own Test.
export const SqmeterUrlField: FunctionalComponent<SourceFieldProps & { test?: boolean }> = ({ safety, update, error, test = true }) => (
  <>
    <Field label={t('settings.roof.sqmeterUrl')} hint={t('settings.roof.sqmeterUrlHint')} error={error('safety.http.url')}>
      <TextInput
        dataField="safety.http.url"
        value={safety.http.url}
        placeholder={t('settings.roof.sqmeterUrlPlaceholder')}
        onInput={(v) => update(['safety', 'http', 'url'], v)}
      />
      {sqmeterHttps(safety.http.url) && <Note>{t('settings.roof.sqmeterHttps')}</Note>}
    </Field>
    <SqmeterLoginFields safety={safety} update={update} error={error} />
    {test && <SqmeterAddressTest safety={safety} />}
  </>
);

// Spec 020: follows SQMeter with the MQTT group (its base topic is where it publishes `<base>/safe`); spec 026: typing unfollows.
export const SqmeterTopicField: FunctionalComponent<SourceFieldProps & Pick<SafetyProps, 'config' | 'status'>> = (props) => {
  const { safety, update, error } = props;
  return (
    <>
      <Field label={t('settings.roof.sqmeterTopic')} error={error('safety.mqtt.topic')}>
        <TextInput dataField="safety.mqtt.topic" value={safety.mqtt.topic} onInput={(v) => update(['safety', 'mqtt', 'topic'], v)} />
      </Field>
      <FollowsSqmeter group="mqtt" {...props} />
    </>
  );
};

// Settings → Roof's card, by the weather sources (whose address it uses). The
// address shows here too while the HTTP weather source is off: the location
// and the copy use it either way.
export const SqmeterCopyCard: FunctionalComponent<SafetyProps> = (props) => (
  <SettingsCard id="sqmeterCopy" title={t('settings.sqmeterCopy.title')} hint={t('settings.sqmeterCopy.hint')}>
    {!props.safety.http.enabled && (
      <div class="form-grid">
        <SqmeterUrlField safety={props.safety} update={props.update} error={props.error} />
      </div>
    )}
    {/* The address as typed, saved or not (as the wizard does). */}
    <SqmeterCopy {...props} url={props.safety.http.url} />
  </SettingsCard>
);

const SafetyCard: FunctionalComponent<SafetyProps> = (props) => (
  <SettingsCard id="safety" title={t('settings.roof.weatherSources')} hint={t('settings.roof.weatherSourcesHint')}>
    <WeatherSourceFields {...props} />
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

const ParkCard: FunctionalComponent<ParkProps> = (props) => (
  <SettingsCard id="park" title={t('settings.roof.park')} hint={t('settings.roof.parkHint')}>
    {/* Spec 022: a RoofRoof pointed at a N.I.N.A. asks it to park when unsafe. */}
    <Note tone="warn">{t('settings.roof.liveNinaWarning')}</Note>
    <div class="form-grid">
      <ParkSourceField {...props} />
      <ParkSwitchesField {...props} />
      <NinaUrlField {...props} />
      <RequestParkToggle {...props} />
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
    <ParkNotes {...props} />
  </SettingsCard>
);

export type DaylightProps = SettingsTabProps & { daylight: DaylightConfig; location: LocationConfig };
export { LocationFields };

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
      <SunAltitudeField daylight={daylight} update={update} error={error} />
    </div>
    <DepNote entry={deps.get('daylight.mode')} onFix={fix} />
    <DepNote entry={deps.get('daylight.sunAltitudeDeg')} onFix={fix} />
  </>
);

// The sun's altitude limit (also the setup wizard's daylight screen, spec 018).
export const SunAltitudeField: FunctionalComponent<Pick<DaylightProps, 'daylight' | 'update' | 'error'>> = ({
  daylight,
  update,
  error,
}) => (
  <DegreesField
    at={['daylight', 'sunAltitudeDeg']}
    label={t('settings.roof.sunAltitudeLimit')}
    hint={t('settings.roof.sunAltitudeLimitHint')}
    value={daylight.sunAltitudeDeg}
    update={update}
    error={error}
  />
);

const DaylightCard: FunctionalComponent<DaylightProps> = (props) => (
  <SettingsCard id="daylight" title={t('settings.roof.daylight')} hint={t('settings.roof.daylightHint')}>
    <DaylightModeFields {...props} />
    <LocationFields
      {...props}
      sqmeterUrl={props.config.safety?.http.url ?? ''}
      onSetAddress={() => props.goTo('roof', props.config.safety?.http.enabled ? 'safety' : 'sqmeterCopy')}
    />
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

// Spec 023: the board and its pins, set on their own page; they apply at a restart.
export const PinsCard: FunctionalComponent<SettingsTabProps> = ({ config }) => (
  <SettingsCard id="pins" title={t('pins.title')} hint={t('settings.roof.pinsHint')}>
    {config.pins && <ReadingRow label={t('pins.presetLabel')} value={presetLabel(config.pins.preset)} />}
    {config.pins?.preset === 'unset' && <Note tone="warn">{t('pins.unsetNote')}</Note>}
    <div class="btn-row">
      <Button onClick={() => route('/pins')}>{t('pins.open')}</Button>
    </div>
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

// Spec 026: the roof's cards in the order it is set up (the stages above them), each setting in one place.
const RoofTab: FunctionalComponent<SettingsTabProps> = (props) => {
  const { roof, safety, park, daylight, location, autoClose, mqtt } = props.config;
  const stages = setupStages(props.config);
  return (
    <>
      <MotionNote status={props.status} />
      {stages.length > 0 && (
        <SettingsCard
          id="setup"
          title={t('setupProgress.title')}
          hint={t('setupProgress.hint')}
          badge={<Pill tone="pill-dim">{stagesDone(stages)}</Pill>}
        >
          <SetupStages stages={stages} />
        </SettingsCard>
      )}
      <PinsCard {...props} />
      {safety && <SafetyCard {...props} safety={safety} />}
      {safety && <SqmeterCopyCard {...props} safety={safety} />}
      {park && <ParkCard {...props} park={park} />}
      {roof ? (
        <>
          <MotionCard {...props} roof={roof} />
          <HallCard {...props} roof={roof} />
          <TravelCard {...props} roof={roof} />
          <CurrentCard {...props} roof={roof} />
        </>
      ) : (
        <Note>{t('settings.roof.notSent')}</Note>
      )}
      {autoClose && <AutoCloseCard {...props} autoClose={autoClose} mqtt={mqtt} />}
      {daylight && location && <DaylightCard {...props} daylight={daylight} location={location} />}
      {props.config.testBox && <TestBoxCard {...props} testBox={props.config.testBox} />}
    </>
  );
};

export default RoofTab;
