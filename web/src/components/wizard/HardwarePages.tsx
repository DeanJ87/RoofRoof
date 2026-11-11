import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { RoofStatus } from '../../types';
import type { WizardPageProps } from './context';
import { CurrentCard, HallCard, ParkSwitchesField, RainContactToggle, roofNum } from '../settings/RoofTab';
import { ActionButton, Field, NumberInput, SettingsCard, Toggle } from '../settings/controls';
import { Note, ReadingRow } from '../ui';
import { docsLink } from '../../lib/docsLinks';
import { t } from '../../i18n';
import { formatNumber } from '../../i18n/format';

// Pages 3 to 5 (spec 015 FR-008): the hardware with its live readings, the
// Hall sensor, and the roof's motion with the first-run values. Nothing here
// moves the roof; the readings are what the device reads now.

// The commissioning guide's stage 2 tolerance between the reading and a meter.
export const METER_TOLERANCE_V = 0.2;

export const meterCheck = (reading: number | null, meter: number): 'none' | 'agrees' | 'differs' => {
  if (reading === null || !Number.isFinite(meter) || meter <= 0) return 'none';
  return Math.abs(reading - meter) <= METER_TOLERANCE_V + 1e-9 ? 'agrees' : 'differs';
};

const SWITCH_TEXT = { closed: 'roof.switch.closed', open: 'roof.switch.open', unreadable: 'roof.switch.unreadable' } as const;
const RAIN_TEXT = { dry: 'roof.rain.dry', raining: 'roof.rain.raining', unreadable: 'roof.rain.unreadable' } as const;

const switchText = (reading: RoofStatus['parkSwitches']['switch1'] | undefined) =>
  reading === undefined ? t('wizard.noReading') : reading === 'notFitted' ? t('wizard.saveToRead') : t(SWITCH_TEXT[reading]);

const rainText = (reading: RoofStatus['rainContact'] | undefined) =>
  reading === undefined ? t('wizard.noReading') : reading === 'notFitted' ? t('wizard.saveToRead') : t(RAIN_TEXT[reading]);

const SupplyCheck: FunctionalComponent<{ roof: RoofStatus | undefined }> = ({ roof }) => {
  const [meter, setMeter] = useState(Number.NaN);
  const reading = roof ? roof.supplyVolts : null;
  const check = meterCheck(reading, meter);
  return (
    <>
      <ReadingRow
        label={t('wizard.hardware.supply')}
        value={
          !roof ? t('wizard.noReading') : reading === null ? t('roof.unreadable') : t('roof.volts', { value: formatNumber(reading, 2) })
        }
      />
      <ReadingRow label={t('wizard.hardware.divider')} value="100k/22k" />
      <Field label={t('wizard.hardware.meterReading')} hint={t('wizard.hardware.meterReadingHint')}>
        <NumberInput dataField="wizard.meter" value={meter} min={0} unit="V" onChange={setMeter} />
      </Field>
      {check === 'agrees' && <Note tone="ok">{t('wizard.hardware.meterAgrees')}</Note>}
      {check === 'differs' && reading !== null && (
        <Note tone="warn">{t('wizard.hardware.meterDiffers', { difference: formatNumber(Math.abs(reading - meter), 1) })}</Note>
      )}
    </>
  );
};

// The park switches and the rain contact, each with its live state.
const InputsCard: FunctionalComponent<WizardPageProps> = (props) => {
  const roof = props.status?.roof;
  const { park, safety } = props.config;
  const fitted = park ? { none: 0, one: 1, two: 2 }[park.switches] : 0;
  return (
    <SettingsCard id="inputs" title={t('wizard.hardware.inputsTitle')} hint={t('wizard.hardware.inputsHint')}>
      {park && <ParkSwitchesField {...props} park={park} />}
      {fitted >= 1 && <ReadingRow label={t('roof.parkSwitch1')} value={switchText(roof?.parkSwitches.switch1)} />}
      {fitted >= 2 && <ReadingRow label={t('roof.parkSwitch2')} value={switchText(roof?.parkSwitches.switch2)} />}
      {safety && <RainContactToggle {...props} safety={safety} />}
      {safety?.rainContact && <ReadingRow label={t('roof.rainContact')} value={rainText(roof?.rainContact)} />}
    </SettingsCard>
  );
};

const motorCurrentText = (roof: RoofStatus | undefined) =>
  roof?.motorAmps == null ? t('wizard.noReading') : t('roof.amps', { value: formatNumber(roof.motorAmps, 1) });

export const HardwarePage: FunctionalComponent<WizardPageProps> = (props) => (
  <>
    <SettingsCard id="supply" title={t('wizard.hardware.supplyTitle')} hint={t('wizard.hardware.supplyHint')}>
      <SupplyCheck roof={props.status?.roof} />
    </SettingsCard>
    {props.config.roof && <CurrentCard {...props} roof={props.config.roof} />}
    {props.config.roof?.currentSenseEnabled && (
      <Note>{t('wizard.hardware.motorCurrent', { value: motorCurrentText(props.status?.roof) })}</Note>
    )}
    <InputsCard {...props} />
  </>
);

export const HallPage: FunctionalComponent<WizardPageProps> = (props) => (
  <>
    <SettingsCard id="hall-fitted" title={t('wizard.hall.title')} hint={t('wizard.hall.hint')}>
      <Toggle
        dataField="wizard.hallNotFitted"
        label={t('wizard.hall.notFitted')}
        hint={t('wizard.hall.notFittedHint')}
        checked={props.hallNotFitted}
        onChange={props.setHallNotFitted}
      />
      {props.hallNotFitted && (
        <>
          <Note tone="bad">{t('wizard.hall.noAutomaticClose')}</Note>
          <Note>
            <a href={docsLink('hardware/quick-bench-test.md')} target="_blank" rel="noopener noreferrer">
              {t('wizard.hall.benchTestLink')}
            </a>
          </Note>
        </>
      )}
    </SettingsCard>
    {!props.hallNotFitted && props.config.roof && <HallCard {...props} roof={props.config.roof} />}
  </>
);

// The factory motion values: until they are measured, the first moves run at the slow duty.
export const FIRST_RUN_TOP_DUTY = 40;
const isFactoryMotion = (roof: NonNullable<WizardPageProps['config']['roof']>) =>
  roof.travelMm === 1850 && roof.speedMmPerSecond === 38 && roof.topDutyPercent === 100;

export const MotionPage: FunctionalComponent<WizardPageProps> = (props) => {
  const roof = props.config.roof;
  if (!roof) return <Note>{t('settings.roof.notSent')}</Note>;
  const rp = { ...props, roof };
  return (
    <SettingsCard id="motion" title={t('wizard.motion.title')} hint={t('wizard.motion.hint')}>
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
    </SettingsCard>
  );
};
