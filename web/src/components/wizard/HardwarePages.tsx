import { FunctionalComponent } from 'preact';
import type { RoofStatus } from '../../types';
import { choiceOf, type WizardPageProps } from './context';
import { HallFields, roofNum } from '../settings/RoofTab';
import { RequestParkToggle } from '../settings/TestBoxCard';
import { ActionButton } from '../settings/controls';
import { ChoiceGroup, Note, ReadingRow } from '../ui';
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
  const sqmeterOn = Boolean(props.config.safety?.http.enabled || props.config.safety?.mqtt.enabled);
  const current = box.park || box.weather || box.daylight ? 'testBox' : 'telescope';
  const choice = choiceOf(props, 'testBox', current, (v) => {
    const on = v === 'testBox';
    props.updateMany([
      [['testBox', 'park'], on && !ninaSet],
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

export const ParkSwitchesScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { park } = props.config;
  if (!park) return <Note>{t('settings.roof.notSent')}</Note>;
  const roof = props.status?.roof;
  const fitted = { none: 0, one: 1, two: 2 }[park.switches];
  const choice = choiceOf(props, 'parkSwitches', park.switches, (v) => props.update(['park', 'switches'], v));
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'none', icon: 'off', title: t('wizard.hardware.switchesNone'), detail: t('wizard.hardware.switchesNoneDetail') },
          { value: 'one', icon: 'switch', title: t('wizard.hardware.switchesOne'), detail: t('wizard.hardware.switchesOneDetail') },
          { value: 'two', icon: 'switch', title: t('wizard.hardware.switchesTwo'), detail: t('wizard.hardware.switchesTwoDetail') },
        ]}
      />
      {/* Spec 019: the switches are an optional second check. */}
      <Note>{t('wizard.hardware.parkOptional')}</Note>
      {choice.value && fitted >= 1 && <ReadingRow label={t('roof.parkSwitch1')} value={switchText(roof?.parkSwitches.switch1)} />}
      {choice.value && fitted >= 2 && <ReadingRow label={t('roof.parkSwitch2')} value={switchText(roof?.parkSwitches.switch2)} />}
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
