import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { ParkConfig, ParkSource, ParkSwitches } from '../../types';
import type { SettingsTabProps } from './context';
import { Field, SelectInput, TextInput } from './controls';
import { route } from 'preact-router';
import { Note } from '../ui';
import { TEST_BOX_PATH } from '../TestBoxBanner';
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
// Switches only turns the test box's park bypass off (it stands in for N.I.N.A.), saying so.
export const ParkSourceField: FunctionalComponent<ParkProps> = ({ park, config, updateMany, error }) => {
  const [cleared, setCleared] = useState(false);
  const choose = (source: ParkSource) => {
    const changes = parkSourceChanges(park, source, config.testBox);
    setCleared(changes.some(([path]) => path.join('.') === 'testBox.park'));
    updateMany(changes);
  };
  return (
    <Field label={t('settings.roof.parkSource')} hint={t('settings.roof.parkSourceHint')} error={error('park.source')}>
      <SelectInput
        dataField="park.source"
        value={parkSource(park)}
        options={[
          { value: 'nina', label: t('settings.roof.sourceNina') },
          { value: 'switches', label: t('settings.roof.sourceSwitches') },
          { value: 'ninaAndSwitches', label: t('settings.roof.sourceBoth') },
        ]}
        onChange={(v) => choose(v as ParkSource)}
      />
      {cleared && !config.testBox?.park && (
        <Note tone="warn">
          <span data-park-note="testBoxCleared">{t('settings.roof.parkNote.testBoxCleared')}</span>
        </Note>
      )}
    </Field>
  );
};

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
      <Note
        key={warning}
        tone={warning === 'wiring' ? 'muted' : 'warn'}
        action={warning === 'testBox' ? { label: t('testBox.entry.open'), onClick: () => route(TEST_BOX_PATH) } : undefined}
      >
        <span data-park-note={warning}>{t(`settings.roof.parkNote.${warning}`)}</span>
      </Note>
    ))}
    {nina && !ninaCounts(park) && <Note>{t('settings.roof.ninaOptional')}</Note>}
  </>
);
