import { FunctionalComponent } from 'preact';
import type { ParkConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { Field, SelectInput, TextInput } from './controls';
import { t } from '../../i18n';

// The park settings' fields, shared by Settings → Roof and the setup wizard.

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
