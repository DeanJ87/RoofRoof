import { FunctionalComponent } from 'preact';
import type { ParkConfig, ParkSource, ParkSwitches } from '../../types';
import type { SettingsTabProps } from './context';
import { Field, SelectInput, TextInput } from './controls';
import { Note } from '../ui';
import { ninaCounts, parkSource, parkSourceChanges, parkSwitchesChanges, parkWarnings } from '../../lib/parkSource';
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

// Spec 024: how the park is known (N.I.N.A., the switches, or both); the source and the count always change together.
export const ParkSourceField: FunctionalComponent<ParkProps> = ({ park, updateMany, error }) => (
  <Field label={t('settings.roof.parkSource')} hint={t('settings.roof.parkSourceHint')} error={error('park.source')}>
    <SelectInput
      dataField="park.source"
      value={parkSource(park)}
      options={[
        { value: 'nina', label: t('settings.roof.sourceNina') },
        { value: 'switches', label: t('settings.roof.sourceSwitches') },
        { value: 'ninaAndSwitches', label: t('settings.roof.sourceBoth') },
      ]}
      onChange={(v) => updateMany(parkSourceChanges(park, v as ParkSource))}
    />
  </Field>
);

// How many switches are fitted: shown only for a switch source (N.I.N.A. only fits none).
export const ParkSwitchesField: FunctionalComponent<ParkProps> = ({ park, updateMany, error }) =>
  parkSource(park) === 'nina' ? null : (
    <Field label={t('settings.roof.parkSwitches')} hint={t('settings.roof.parkSwitchesHint')} error={error('park.switches')}>
      <SelectInput
        dataField="park.switches"
        value={park.switches}
        options={[
          { value: 'one', label: t('settings.roof.switchesOne') },
          { value: 'two', label: t('settings.roof.switchesTwo') },
        ]}
        onChange={(v) => updateMany(parkSwitchesChanges(park, v as ParkSwitches))}
      />
    </Field>
  );

// Spec 024: with switches only, the wiring guards and the optional N.I.N.A. address (Settings and the wizard).
export const ParkNotes: FunctionalComponent<ParkProps & { nina?: boolean }> = ({ park, config, nina = true }) => (
  <>
    {parkWarnings(park, config.testBox).map((warning) => (
      <Note key={warning} tone={warning === 'wiring' ? 'muted' : 'warn'}>
        <span data-park-note={warning}>{t(`settings.roof.parkNote.${warning}`)}</span>
      </Note>
    ))}
    {nina && !ninaCounts(park) && <Note>{t('settings.roof.ninaOptional')}</Note>}
  </>
);
