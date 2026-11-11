import { FunctionalComponent } from 'preact';
import type { AutoCloseConfig, MQTTConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { DepNote, Field, Fold, NumberInput, SelectInput, Toggle } from './controls';
import { SettingsCard } from './controls';
import { Note } from '../ui';
import { t } from '../../i18n';
import { NINA_ROOF_DOC } from '../../lib/docsLinks';

// Settings → Roof → Automatic close (spec 014): the policy specs 005 and 013
// fixed, as settings. The defaults are those specs' values; the ranges are the
// device's (configSchema.ts, the shared range fixture). The four an owner is
// most likely to change are in view, the rest in the Advanced fold.

type Props = SettingsTabProps & { autoClose: AutoCloseConfig; mqtt: MQTTConfig };

const ADVANCED: (keyof AutoCloseConfig)[] = [
  'parkRequestSeconds',
  'parkHoldSeconds',
  'retryMinutes',
  'closeStartSeconds',
  'closeGuardSeconds',
];

const PolicyNumber: FunctionalComponent<{
  props: Props;
  at: string[];
  label: string;
  hint: string;
  unit: string;
  value: number;
}> = ({ props, at, label, hint, unit, value }) => (
  <Field label={label} hint={hint} error={props.error(at.join('.'))}>
    <NumberInput dataField={at.join('.')} value={value} min={0} unit={unit} integer onChange={(v) => props.update(at, v)} />
  </Field>
);

const advancedOpen = (props: Props) =>
  ADVANCED.some((key) => props.error(`autoClose.${key}`) !== undefined) || props.error('mqtt.changeMinSeconds') !== undefined;

const AdvancedRows: FunctionalComponent<Props> = (props) => {
  const a = props.autoClose;
  const sec = t('settings.roof.unitSeconds');
  const min = t('settings.roof.unitMinutes');
  return (
    <Fold label={t('settings.roof.advanced')} open={advancedOpen(props)} dataField="autoClose.advanced">
      <div class="form-grid">
        <PolicyNumber
          props={props}
          at={['autoClose', 'parkRequestSeconds']}
          label={t('settings.roof.parkRequestGap')}
          hint={t('settings.roof.parkRequestGapHint')}
          unit={sec}
          value={a.parkRequestSeconds}
        />
        <PolicyNumber
          props={props}
          at={['autoClose', 'parkHoldSeconds']}
          label={t('settings.roof.parkHold')}
          hint={t('settings.roof.parkHoldHint')}
          unit={sec}
          value={a.parkHoldSeconds}
        />
        <PolicyNumber
          props={props}
          at={['autoClose', 'retryMinutes']}
          label={t('settings.roof.backOff')}
          hint={t('settings.roof.backOffHint')}
          unit={min}
          value={a.retryMinutes}
        />
        <PolicyNumber
          props={props}
          at={['autoClose', 'closeStartSeconds']}
          label={t('settings.roof.closeStart')}
          hint={t('settings.roof.closeStartHint')}
          unit={sec}
          value={a.closeStartSeconds}
        />
        <PolicyNumber
          props={props}
          at={['autoClose', 'closeGuardSeconds']}
          label={t('settings.roof.closeGuard')}
          hint={t('settings.roof.closeGuardHint')}
          unit={sec}
          value={a.closeGuardSeconds}
        />
        <PolicyNumber
          props={props}
          at={['mqtt', 'changeMinSeconds']}
          label={t('settings.roof.mqttChangeGap')}
          hint={t('settings.roof.mqttChangeGapHint')}
          unit={sec}
          value={props.mqtt.changeMinSeconds ?? 1}
        />
      </div>
    </Fold>
  );
};

// Imaging apps open and close the roof over Alpaca, which is off until turned on.
// The same docs anchor as the setup wizard's N.I.N.A. page.
const AlpacaNote: FunctionalComponent<SettingsTabProps> = ({ config, goTo }) =>
  config.alpaca?.enabled ? null : (
    <>
      <Note action={{ label: t('settings.roof.alpacaSettings'), onClick: () => goTo('network', 'alpaca') }}>
        {t('settings.roof.alpacaOffNote')}
      </Note>
      <Note>
        <a href={NINA_ROOF_DOC} target="_blank" rel="noopener noreferrer">
          {t('settings.roof.ninaDocLink')}
        </a>
      </Note>
    </>
  );

const AutoCloseCard: FunctionalComponent<Props> = (props) => {
  const a = props.autoClose;
  return (
    <SettingsCard id="autoClose" title={t('settings.roof.autoClose')} hint={t('settings.roof.autoCloseHint')}>
      <AlpacaNote {...props} />
      <div class="form-grid">
        <PolicyNumber
          props={props}
          at={['autoClose', 'rearmMinutes']}
          label={t('settings.roof.rearm')}
          hint={t('settings.roof.rearmHint')}
          unit={t('settings.roof.unitMinutes')}
          value={a.rearmMinutes}
        />
        <PolicyNumber
          props={props}
          at={['autoClose', 'remindMinutes']}
          label={t('settings.roof.remind')}
          hint={t('settings.roof.remindHint')}
          unit={t('settings.roof.unitMinutes')}
          value={a.remindMinutes}
        />
        <Field label={t('settings.roof.notStarted')} hint={t('settings.roof.notStartedHint')} error={props.error('autoClose.notStarted')}>
          <SelectInput
            dataField="autoClose.notStarted"
            value={a.notStarted}
            options={[
              { value: 'halt', label: t('settings.roof.notStartedHalt') },
              { value: 'retry', label: t('settings.roof.notStartedRetry') },
            ]}
            onChange={(v) => props.update(['autoClose', 'notStarted'], v)}
          />
        </Field>
      </div>
      <Toggle
        dataField="autoClose.remoteStop"
        label={t('settings.roof.remoteStop')}
        hint={t('settings.roof.remoteStopHint')}
        checked={a.remoteStop}
        onChange={(v) => props.update(['autoClose', 'remoteStop'], v)}
      />
      <DepNote entry={props.deps.get('autoClose.remoteStop')} onFix={props.fix} />
      <AdvancedRows {...props} />
    </SettingsCard>
  );
};

export default AutoCloseCard;
