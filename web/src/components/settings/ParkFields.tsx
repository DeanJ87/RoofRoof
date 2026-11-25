import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { ParkConfig, ParkSource, ParkSwitches } from '../../types';
import type { SettingsTabProps } from './context';
import { ActionButton, Field, SelectInput, TextInput } from './controls';
import { route } from 'preact-router';
import { Note, ReadingRow } from '../ui';
import { TEST_BOX_PATH } from '../TestBoxBanner';
import { RequestParkToggle } from './TestBoxCard';
import {
  ninaCounts,
  parkSource,
  parkSourceChanges,
  parkSwitchesChanges,
  parkWarnings,
  testNina,
  type NinaAnswer,
} from '../../lib/parkSource';
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
      placeholder={t('settings.roof.ninaUrlPlaceholder')}
      onInput={(v) => update(['park', 'nina', 'url'], v)}
    />
  </Field>
);

const yesNo = (value: boolean) => t(value ? 'wizard.test.yes' : 'wizard.test.no');

// Spec 027 (FR-013, FR-022): Test reads N.I.N.A.'s mount once at the address as typed, through the
// device: whether N.I.N.A. answered, then the mount connected and parked, or why not.
const NinaTest: FunctionalComponent<{ url: string }> = ({ url }) => {
  const [busy, setBusy] = useState(false);
  const [answer, setAnswer] = useState<NinaAnswer | null>(null);
  const run = async () => {
    setBusy(true);
    setAnswer(null);
    setAnswer(await testNina(url));
    setBusy(false);
  };
  return (
    <div data-field="park.nina.test">
      <div class="btn-row">
        <ActionButton onClick={run} disabled={!/^https?:\/\/./.test(url.trim())} busy={busy} busyLabel={t('common.testing')}>
          {t('settings.roof.sqmeterTest')}
        </ActionButton>
      </div>
      <div role="status" aria-live="polite">
        {answer && <ReadingRow label={t('settings.roof.ninaAnswered')} value={yesNo(answer.ok || answer.reachable)} />}
        {answer?.ok && <ReadingRow label={t('wizard.test.mountConnected')} value={yesNo(answer.connected)} />}
        {answer?.ok && <ReadingRow label={t('settings.roof.mountParked')} value={yesNo(answer.atPark)} />}
        {answer && !answer.ok && <Note tone="bad">{answer.error}</Note>}
      </div>
    </div>
  );
};

// Spec 027 (FR-022): the address, its Test with the answer, then whether the roof may ask N.I.N.A. to park.
export const NinaFields: FunctionalComponent<ParkProps> = (props) => (
  <>
    <NinaUrlField {...props} />
    <NinaTest url={props.park.nina.url} />
    <RequestParkToggle {...props} />
  </>
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
