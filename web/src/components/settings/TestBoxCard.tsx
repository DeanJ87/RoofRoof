import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import type { ParkConfig, TestBoxConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { SettingsCard, Toggle } from './controls';
import { Button, Note, Pill } from '../ui';
import { MODE_PILL, TEST_BOX_PATH, modeOf, testBoxView } from '../TestBoxBanner';
import { t } from '../../i18n';

// Settings → Roof → Test box (spec 022): the way to the test box page, where
// the bypasses are chosen, extended and turned off. Here only whether one is set.

export type TestBoxProps = SettingsTabProps & { testBox: TestBoxConfig };

// Spec 022: whether the automatic close may ask N.I.N.A. to park (also the wizard's test box screen).
export const RequestParkToggle: FunctionalComponent<{ park: ParkConfig; update: SettingsTabProps['update'] }> = ({ park, update }) => (
  <>
    <Toggle
      dataField="park.requestPark"
      label={t('settings.roof.requestPark')}
      hint={t('settings.roof.requestParkHint')}
      checked={park.requestPark ?? true}
      onChange={(v) => update(['park', 'requestPark'], v)}
    />
    {/* Off with an address set: unsafe weather won't ask N.I.N.A. to park (spec 022). */}
    {park.requestPark === false && park.nina.url.trim() !== '' && <Note tone="warn">{t('settings.roof.requestParkOff')}</Note>}
  </>
);

// The way to the test box page; the bypasses are chosen there, not in this tab.
const TestBoxCard: FunctionalComponent<TestBoxProps> = (props) => {
  const live = props.status?.roof?.testBox;
  const view = testBoxView(live);
  const mode = modeOf(live);
  return (
    <SettingsCard
      id="testBox"
      title={t('testBox.title')}
      hint={t('testBox.hint')}
      badge={<Pill tone={MODE_PILL[mode]}>{t(`testBox.mode.${mode}`)}</Pill>}
    >
      <Note tone={view?.tone ?? (mode === 'unknown' ? 'warn' : 'muted')}>
        {view ? view.detail : t(mode === 'unknown' ? 'testBox.mode.unknownNote' : 'testBox.mode.offNote')}
      </Note>
      <div class="btn-row">
        <Button onClick={() => route(TEST_BOX_PATH)}>{t('testBox.entry.open')}</Button>
      </div>
    </SettingsCard>
  );
};

export default TestBoxCard;
