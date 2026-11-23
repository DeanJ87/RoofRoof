import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useState } from 'preact/hooks';
import type { ParkSource, ParkSwitches, RoofStatus } from '../../types';
import { choiceOf, type WizardPageProps } from './context';
import { CurrentFields, HallFields, ParkNotes, roofNum } from '../settings/RoofTab';
import { RequestParkToggle } from '../settings/TestBoxCard';
import { ActionButton, Field, NumberInput } from '../settings/controls';
import { Button, ChoiceGroup, Note, ReadingRow } from '../ui';
import { docsLink } from '../../lib/docsLinks';
import { fittedCount, parkSource, parkSourceChanges, parkSwitchesChanges } from '../../lib/parkSource';
import { t } from '../../i18n';
import { formatNumber } from '../../i18n/format';

// The Hardware, Hall sensor, Calibration run and Roof motion pages' screens
// (spec 015 FR-008, spec 018 FR-007). Nothing here moves the roof; the
// readings are what the device reads now.

// The commissioning guide's stage 2 tolerance between the reading and a meter.
export const METER_TOLERANCE_V = 0.2;

export const meterCheck = (reading: number | null, meter: number): 'none' | 'agrees' | 'differs' => {
  if (reading === null || !Number.isFinite(meter) || meter <= 0) return 'none';
  return Math.abs(reading - meter) <= METER_TOLERANCE_V + 1e-9 ? 'agrees' : 'differs';
};

const SWITCH_TEXT = { closed: 'roof.switch.closed', open: 'roof.switch.open', unreadable: 'roof.switch.unreadable' } as const;

const switchText = (reading: RoofStatus['parkSwitches']['switch1'] | undefined) =>
  reading === undefined ? t('wizard.noReading') : reading === 'notFitted' ? t('wizard.saveToRead') : t(SWITCH_TEXT[reading]);

// The live supply against the owner's meter (never saved), and the low limit.
export const SupplyScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const roof = props.status?.roof;
  const [meter, setMeter] = useState(Number.NaN);
  const reading = roof ? roof.supplyVolts : null;
  const check = meterCheck(reading, meter);
  return (
    <div id="supply" class="card-group">
      <ReadingRow
        label={t('wizard.hardware.supply')}
        value={
          !roof ? t('wizard.noReading') : reading === null ? t('roof.unreadable') : t('roof.volts', { value: formatNumber(reading, 2) })
        }
      />
      <ReadingRow label={t('wizard.hardware.divider')} value="100k/22k" />
      <div class="form-grid">
        <Field label={t('wizard.hardware.meterReading')} hint={t('wizard.hardware.meterReadingHint')}>
          <NumberInput dataField="wizard.meter" value={meter} min={0} unit="V" onChange={setMeter} />
        </Field>
        {props.config.roof &&
          roofNum({ ...props, roof: props.config.roof }, 'minSupplyVolts', t('settings.roof.minSupply'), {
            unit: 'V',
            hint: t('settings.roof.minSupplyHint'),
          })}
      </div>
      {check === 'agrees' && <Note tone="ok">{t('wizard.hardware.meterAgrees')}</Note>}
      {check === 'differs' && reading !== null && (
        <Note tone="warn">{t('wizard.hardware.meterDiffers', { difference: formatNumber(Math.abs(reading - meter), 1) })}</Note>
      )}
    </div>
  );
};

const motorCurrentText = (roof: RoofStatus | undefined) =>
  roof?.motorAmps == null ? t('wizard.noReading') : t('roof.amps', { value: formatNumber(roof.motorAmps, 1) });

export const CurrentScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const roof = props.config.roof;
  if (!roof) return <Note>{t('settings.roof.notSent')}</Note>;
  const choice = choiceOf(props, 'current', roof.currentSenseEnabled ? 'yes' : 'no', (v) =>
    props.update(['roof', 'currentSenseEnabled'], v === 'yes'),
  );
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'yes', icon: 'bolt', title: t('wizard.fitted'), detail: t('wizard.hardware.currentYes') },
          { value: 'no', icon: 'off', title: t('wizard.notFittedYet'), detail: t('wizard.hardware.currentNo') },
        ]}
      />
      {roof.currentSenseEnabled && (
        <>
          <ReadingRow label={t('wizard.hardware.motorCurrent')} value={motorCurrentText(props.status?.roof)} />
          <div class="form-grid">
            <CurrentFields {...props} roof={roof} />
          </div>
        </>
      )}
    </>
  );
};

// What a test box answer says, and whether it may ask N.I.N.A. to park.
const TestBoxNotes: FunctionalComponent<WizardPageProps & { hours: number; ninaSet: boolean; sqmeterOn: boolean }> = (props) => (
  <>
    <Note tone="bad">{t('wizard.testBox.warning', { hours: props.hours })}</Note>
    {props.ninaSet && <Note tone="warn">{t('wizard.testBox.ninaSet')}</Note>}
    {props.sqmeterOn && <Note tone="warn">{t('wizard.testBox.sqmeterOn')}</Note>}
    {props.config.park && <RequestParkToggle park={props.config.park} update={props.update} />}
  </>
);

// Spec 022: a test box (no telescope, no weather station yet) gets the
// bypasses, each only where its source isn't set up; a telescope gets none.
export const TestBoxScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const box = props.config.testBox;
  if (!box) return <Note>{t('settings.roof.notSent')}</Note>;
  const ninaSet = Boolean(props.config.park?.nina.url.trim());
  // Spec 024: with park switches only there is no N.I.N.A. to stand in for.
  const switchesOnly = props.config.park ? parkSource(props.config.park) === 'switches' : false;
  const sqmeterOn = Boolean(props.config.safety?.http.enabled || props.config.safety?.mqtt.enabled);
  const current = box.park || box.weather || box.daylight ? 'testBox' : 'telescope';
  const choice = choiceOf(props, 'testBox', current, (v) => {
    const on = v === 'testBox';
    props.updateMany([
      [['testBox', 'park'], on && !ninaSet && !switchesOnly],
      [['testBox', 'weather'], on && !sqmeterOn],
      [['testBox', 'daylight'], on],
    ]);
  });
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'telescope', icon: 'home', title: t('wizard.testBox.telescope'), detail: t('wizard.testBox.telescopeDetail') },
          { value: 'testBox', icon: 'warning', title: t('wizard.testBox.testBox'), detail: t('wizard.testBox.testBoxDetail') },
        ]}
      />
      {choice.value === 'testBox' && <TestBoxNotes {...props} hours={box.hours} ninaSet={ninaSet} sqmeterOn={sqmeterOn} />}
    </>
  );
};

// Spec 024: how the park is known; the switch count is the next screen (switch sources only).
export const ParkSwitchesScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { park } = props.config;
  if (!park) return <Note>{t('settings.roof.notSent')}</Note>;
  const choice = choiceOf(props, 'parkSource', parkSource(park), (v) =>
    props.updateMany(parkSourceChanges(park, v as ParkSource, props.config.testBox)),
  );
  return (
    <ChoiceGroup
      {...choice}
      options={[
        { value: 'nina', icon: 'scope', title: t('settings.roof.sourceNina'), detail: t('wizard.hardware.sourceNinaDetail') },
        { value: 'switches', icon: 'switch', title: t('settings.roof.sourceSwitches'), detail: t('wizard.hardware.sourceSwitchesDetail') },
        { value: 'ninaAndSwitches', icon: 'switch', title: t('settings.roof.sourceBoth'), detail: t('wizard.hardware.sourceBothDetail') },
      ]}
    />
  );
};

// Spec 024: one or two switches, with their live readings and the switches-only warnings.
export const ParkSwitchCountScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { park } = props.config;
  if (!park) return <Note>{t('settings.roof.notSent')}</Note>;
  const roof = props.status?.roof;
  const fitted = fittedCount(park.switches);
  const choice = choiceOf(props, 'parkSwitches', park.switches === 'none' ? null : park.switches, (v) =>
    props.updateMany(parkSwitchesChanges(park, v as ParkSwitches)),
  );
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'one', icon: 'switch', title: t('wizard.hardware.switchesOne'), detail: t('wizard.hardware.switchesOneDetail') },
          { value: 'two', icon: 'switch', title: t('wizard.hardware.switchesTwo'), detail: t('wizard.hardware.switchesTwoDetail') },
        ]}
      />
      {choice.value && fitted >= 1 && <ReadingRow label={t('roof.parkSwitch1')} value={switchText(roof?.parkSwitches.switch1)} />}
      {choice.value && fitted >= 2 && <ReadingRow label={t('roof.parkSwitch2')} value={switchText(roof?.parkSwitches.switch2)} />}
      {choice.value && <ParkNotes {...props} park={park} nina={false} />}
    </>
  );
};

export const HallFittedScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const choice = choiceOf(props, 'hall', props.hallNotFitted ? 'no' : 'yes', (v) => props.setHallNotFitted(v === 'no'));
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'yes', icon: 'magnet', title: t('wizard.fitted'), detail: t('wizard.hall.yesDetail') },
          { value: 'no', icon: 'off', title: t('wizard.notFittedYet'), detail: t('wizard.hall.noDetail') },
        ]}
      />
      {choice.value === 'no' && (
        <>
          <Note tone="bad">{t('wizard.hall.noAutomaticClose')}</Note>
          <Note>
            <a href={docsLink('hardware/quick-bench-test.md')} target="_blank" rel="noopener noreferrer">
              {t('wizard.hall.benchTestLink')}
            </a>
          </Note>
        </>
      )}
    </>
  );
};

export const HallScreen: FunctionalComponent<WizardPageProps> = (props) =>
  props.config.roof ? <HallFields {...props} roof={props.config.roof} /> : <Note>{t('settings.roof.notSent')}</Note>;

// Spec 019: teaching the ends is offered here, but the jog runs on the
// calibration page's Teach card (with Stop): this screen moves nothing.
export const TeachScreen: FunctionalComponent<WizardPageProps> = () => (
  <>
    <Note>{t('wizard.teach.what')}</Note>
    <Note tone="warn">{t('wizard.teach.needs')}</Note>
    <div class="btn-row">
      <Button onClick={() => route('/calibrate')}>{t('wizard.teach.open')}</Button>
    </div>
    <Note>{t('wizard.teach.later')}</Note>
  </>
);

// Spec 016: the calibration run is offered here, but runs on its own attended
// page (with Stop): this screen has no motion control of its own.
export const CalibrationScreen: FunctionalComponent<WizardPageProps> = () => (
  <>
    <Note>{t('wizard.calibrate.what')}</Note>
    <Note tone="warn">{t('wizard.calibrate.needs')}</Note>
    <div class="btn-row">
      <Button onClick={() => route('/calibrate')}>{t('calibrate.open')}</Button>
    </div>
    <Note>{t('wizard.calibrate.later')}</Note>
  </>
);

// The factory motion values: until they are measured, the first moves run at the slow duty.
export const FIRST_RUN_TOP_DUTY = 40;
const isFactoryMotion = (roof: NonNullable<WizardPageProps['config']['roof']>) =>
  roof.travelMm === 1850 && roof.speedMmPerSecond === 38 && roof.topDutyPercent === 100;

export const MotionScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const roof = props.config.roof;
  if (!roof) return <Note>{t('settings.roof.notSent')}</Note>;
  const rp = { ...props, roof };
  return (
    <div id="motion" class="card-group">
      <div class="form-grid">
        {roofNum(rp, 'travelMm', t('settings.roof.travel'), { unit: 'mm', integer: true, hint: t('settings.roof.travelHint') })}
        {roofNum(rp, 'speedMmPerSecond', t('settings.roof.speed'), { unit: 'mm/s', hint: t('settings.roof.speedHint') })}
        {roofNum(rp, 'topDutyPercent', t('settings.roof.topDuty'), { unit: '%', integer: true, hint: t('wizard.motion.topDutyHint') })}
      </div>
      {isFactoryMotion(roof) && (
        <div class="btn-row">
          <ActionButton onClick={() => props.update(['roof', 'topDutyPercent'], FIRST_RUN_TOP_DUTY)}>
            {t('wizard.motion.useFirstRun')}
          </ActionButton>
        </div>
      )}
      <Note>{t('wizard.motion.measureInCommissioning')}</Note>
    </div>
  );
};
