import { FunctionalComponent } from 'preact';
import type { ParkSource, ParkSwitches, RoofStatus } from '../../types';
import type { ConfigPath } from '../settings/context';
import { choiceOf, type WizardPageProps } from './context';
import { HallFields, ParkNotes, roofNum } from '../settings/RoofTab';
import { RequestParkToggle } from '../settings/TestBoxCard';
import { ActionButton } from '../settings/controls';
import { ChoiceGroup, Note, ReadingRow } from '../ui';
import { fittedCount, parkBypassChanges, parkSource, parkSourceChanges, parkSwitchesChanges } from '../../lib/parkSource';
import { t } from '../../i18n';

// The test box, park switches and typed motor values screens (specs 015, 018,
// 022, 025). Nothing here moves the roof; the readings are what the device reads now.

const SWITCH_TEXT = { closed: 'roof.switch.closed', open: 'roof.switch.open', unreadable: 'roof.switch.unreadable' } as const;

const switchText = (reading: RoofStatus['parkSwitches']['switch1'] | undefined) =>
  reading === undefined ? t('wizard.noReading') : reading === 'notFitted' ? t('wizard.saveToRead') : t(SWITCH_TEXT[reading]);

// What a test box answer says, and whether it may ask N.I.N.A. to park.
const TestBoxNotes: FunctionalComponent<WizardPageProps & { hours: number; ninaSet: boolean; sqmeterOn: boolean }> = (props) => (
  <>
    <Note tone="bad">{t('wizard.testBox.warning', { hours: props.hours })}</Note>
    {props.ninaSet && <Note tone="warn">{t('testBox.ninaCleared')}</Note>}
    {props.sqmeterOn && <Note tone="warn">{t('wizard.testBox.sqmeterOn')}</Note>}
    <Note>{t('wizard.testBox.later')}</Note>
    {props.config.park && <RequestParkToggle park={props.config.park} update={props.update} />}
  </>
);

// Spec 022: a test box (no telescope, no weather station yet) gets the
// bypasses (the weather one only with no SQMeter source on); a telescope gets
// none. Owner decision 2026-11-24: "No telescope yet" clears the N.I.N.A. address.
export const TestBoxScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const box = props.config.testBox;
  if (!box) return <Note>{t('settings.roof.notSent')}</Note>;
  // Set in the settings as loaded: the wizard's own edit clears it for the bench answer.
  const savedUrl = props.saved.park?.nina.url ?? '';
  const ninaSet = savedUrl.trim() !== '';
  // Spec 024: with park switches only there is no N.I.N.A. to stand in for.
  const switchesOnly = props.config.park ? parkSource(props.config.park) === 'switches' : false;
  const sqmeterOn = Boolean(props.config.safety?.http.enabled || props.config.safety?.mqtt.enabled);
  const current = box.park || box.weather || box.daylight ? 'testBox' : 'telescope';
  const choice = choiceOf(props, 'testBox', current, (v) => {
    const on = v === 'testBox';
    props.updateMany([
      ...(switchesOnly
        ? [[['testBox', 'park'], false] as [ConfigPath, unknown]]
        : parkBypassChanges(on, props.config.park?.nina.url ?? '', savedUrl)),
      [['testBox', 'weather'], on && !sqmeterOn],
      [['testBox', 'daylight'], on],
    ]);
  });
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'telescope', icon: 'scope', title: t('wizard.testBox.telescope'), detail: t('wizard.testBox.telescopeDetail') },
          { value: 'testBox', icon: 'warning', title: t('wizard.testBox.testBox'), detail: t('wizard.testBox.testBoxDetail') },
        ]}
      />
      {choice.value === 'testBox' && <TestBoxNotes {...props} hours={box.hours} ninaSet={ninaSet && !switchesOnly} sqmeterOn={sqmeterOn} />}
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

// The factory motion values: until they are measured, the first moves run at the slow duty.
export const FIRST_RUN_TOP_DUTY = 40;
const isFactoryMotion = (roof: NonNullable<WizardPageProps['config']['roof']>) =>
  roof.travelMm === 1850 && roof.speedMmPerSecond === 38 && roof.topDutyPercent === 100;

// "Enter manually instead" (spec 025 FR-015): the values the motor screens
// measure, typed, for a roof without its motor yet.
export const MotionScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const roof = props.config.roof;
  if (!roof) return <Note>{t('settings.roof.notSent')}</Note>;
  const rp = { ...props, roof };
  return (
    <div id="motion" class="card-group">
      <HallFields {...rp} />
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
    </div>
  );
};
