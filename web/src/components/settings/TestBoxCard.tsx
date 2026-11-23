import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { TestBoxConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { DepNote, Field, NumberInput, SettingsCard, Toggle } from './controls';
import { Button, Note } from '../ui';
import { showToast } from '../toast';
import { TEST_BOX_CHANGED, saveConfig } from '../../lib/settingsApi';
import { testBoxText } from '../TestBoxBanner';
import { deviceError } from '../../i18n/deviceMessage';
import { t } from '../../i18n';

// Settings → Roof → Test box (spec 022, constitution 3.0.0): the bypasses for a
// box with no telescope and no weather station. Each can't be switched on
// beside the source it stands in for (the device refuses that save too). The
// deadline is the device's: shown from the status, renewed by sending `hours`.

export type TestBoxProps = SettingsTabProps & { testBox: TestBoxConfig };

// Sends `hours` on its own: the device starts the deadline again from now.
const useRenew = (hours: number) => {
  const [busy, setBusy] = useState(false);
  const renew = async () => {
    setBusy(true);
    try {
      const { ok, body } = await saveConfig({ testBox: { hours } });
      if (ok && body?.success !== false) showToast({ message: t('testBox.renewed', { hours }) });
      else showToast({ message: deviceError(body, t('settings.failedToSaveSettings')), tone: 'bad' });
    } catch {
      showToast({ message: t('settings.couldNotReachTheDevice'), tone: 'bad' });
    } finally {
      setBusy(false);
      window.dispatchEvent(new Event(TEST_BOX_CHANGED));
    }
  };
  return { busy, renew };
};

const TestBoxCard: FunctionalComponent<TestBoxProps> = (props) => {
  const { testBox, config, update, error, goTo, deps, fix } = props;
  const ninaSet = Boolean(config.park?.nina.url.trim());
  const sqmeterOn = Boolean(config.safety?.http.enabled || config.safety?.mqtt.enabled);
  const shown = testBoxText(props.status?.roof?.testBox);
  const anySet = testBox.park || testBox.weather || testBox.daylight;
  const { busy, renew } = useRenew(testBox.hours);
  return (
    <SettingsCard id="testBox" title={t('testBox.title')} hint={t('testBox.hint')}>
      <Note tone="bad">{t('testBox.warning')}</Note>
      <Toggle
        dataField="testBox.park"
        label={t('testBox.park')}
        hint={t('testBox.parkHint')}
        checked={testBox.park}
        onChange={(v) => update(['testBox', 'park'], v)}
        blockedReason={ninaSet ? t('testBox.parkBlocked') : undefined}
        onFix={() => goTo('roof', 'park')}
        fixLabel={t('testBox.goToPark')}
      />
      <Toggle
        dataField="testBox.weather"
        label={t('testBox.weather')}
        hint={t('testBox.weatherHint')}
        checked={testBox.weather}
        onChange={(v) => update(['testBox', 'weather'], v)}
        blockedReason={sqmeterOn ? t('testBox.weatherBlocked') : undefined}
        onFix={() => goTo('roof', 'safety')}
        fixLabel={t('testBox.goToWeather')}
      />
      <Toggle
        dataField="testBox.daylight"
        label={t('testBox.daylight')}
        hint={t('testBox.daylightHint')}
        checked={testBox.daylight}
        onChange={(v) => update(['testBox', 'daylight'], v)}
      />
      <DepNote entry={deps.get('testBox.park')} onFix={fix} />
      <div class="form-grid">
        <Field label={t('testBox.hours')} hint={t('testBox.hoursHint')} error={error('testBox.hours')}>
          <NumberInput
            dataField="testBox.hours"
            value={testBox.hours}
            min={1}
            unit="h"
            integer
            onChange={(v) => update(['testBox', 'hours'], v)}
          />
        </Field>
      </div>
      {shown && <Note tone="bad">{shown}</Note>}
      {anySet && (
        <div class="btn-row">
          <Button onClick={() => void renew()} busy={busy} disabled={props.dirty}>
            {t('testBox.renew')}
          </Button>
        </div>
      )}
    </SettingsCard>
  );
};

export default TestBoxCard;
