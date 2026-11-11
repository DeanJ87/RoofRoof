import { FunctionalComponent } from 'preact';
import type { WizardPageProps } from './context';
import { DaylightModeFields, NinaUrlField, WeatherSourceFields } from '../settings/RoofTab';
import { AlertChannelsCard } from '../settings/AlertChannelsCard';
import { useAlertsView } from '../settings/AlertsTab';
import { Requires, SettingsCard, Toggle } from '../settings/controls';
import { Note } from '../ui';
import { TestPanel } from './TestPanel';
import { NINA_FIELDS, WEATHER_FIELDS } from './pages';
import { t } from '../../i18n';

// Pages 6 to 9 (spec 015 FR-008, FR-009): the weather sources and N.I.N.A.,
// each with a Test; the alert channels with Send test; the daylight mode.

export const WeatherPage: FunctionalComponent<WizardPageProps> = (props) => {
  const { safety } = props.config;
  if (!safety) return <Note>{t('settings.roof.notSent')}</Note>;
  const none = !safety.http.enabled && !safety.mqtt.enabled;
  return (
    <SettingsCard id="weather" title={t('settings.roof.weatherSources')} hint={t('settings.roof.weatherSourcesHint')}>
      <WeatherSourceFields {...props} safety={safety} />
      {none && <Note tone="bad">{t('wizard.weather.noSource')}</Note>}
      <TestPanel part="weather" save={props.saveForTest(WEATHER_FIELDS)} dirty={props.dirty} />
    </SettingsCard>
  );
};

export const NinaPage: FunctionalComponent<WizardPageProps> = (props) => {
  const { park } = props.config;
  const alpacaOn = props.config.alpaca?.enabled ?? false;
  return (
    <>
      {park && (
        <SettingsCard id="nina" title={t('wizard.nina.title')} hint={t('wizard.nina.hint')}>
          <NinaUrlField {...props} park={park} />
          {!park.nina.url && <Note tone="bad">{t('wizard.nina.noUrl')}</Note>}
          <TestPanel part="nina" save={props.saveForTest(NINA_FIELDS)} dirty={props.dirty} />
        </SettingsCard>
      )}
      <SettingsCard id="alpaca" title="ASCOM Alpaca" hint={t('wizard.nina.alpacaHint')}>
        <Toggle
          dataField="alpaca.enabled"
          label={t('wizard.nina.alpaca')}
          checked={alpacaOn}
          onChange={(v) => props.update(['alpaca', 'enabled'], v)}
        />
        <Note>{t('wizard.nina.failsafe')}</Note>
        {!alpacaOn && <Note tone="warn">{t('wizard.nina.alpacaOff')}</Note>}
      </SettingsCard>
    </>
  );
};

export const AlertsPage: FunctionalComponent<WizardPageProps> = (props) => {
  const { view, channelsOn } = useAlertsView(props);
  const silent = view.off || view.channelCount === 0;
  return (
    <>
      <SettingsCard id="alerts" title={t('settings.alerts.alerts')} hint={t('wizard.alerts.hint')}>
        <Toggle
          dataField="alerts.enabled"
          label={t('settings.alerts.sendAlerts')}
          checked={view.alerts.enabled}
          onChange={(v) => view.set(['enabled'], v)}
        />
        {silent && <Requires tone="warn">{t('wizard.alerts.noChannel')}</Requires>}
        {!view.off && channelsOn > 0 && view.dirty && <Note>{t('wizard.alerts.saveToTest')}</Note>}
      </SettingsCard>
      {!view.off && <AlertChannelsCard view={view} />}
    </>
  );
};

export const DaylightPage: FunctionalComponent<WizardPageProps> = (props) => {
  const { daylight } = props.config;
  if (!daylight) return <Note>{t('settings.roof.notSent')}</Note>;
  return (
    <SettingsCard id="daylight" title={t('settings.roof.daylight')} hint={t('settings.roof.daylightHint')}>
      <DaylightModeFields {...props} daylight={daylight} />
      <Note>{t(`wizard.daylight.${daylight.mode}`)}</Note>
      {daylight.mode !== 'off' && !props.config.location?.set && <Note tone="warn">{t('wizard.daylight.needsSite')}</Note>}
    </SettingsCard>
  );
};
