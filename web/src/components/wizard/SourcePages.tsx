import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { AlertChannelName } from '../../types';
import { choiceOf, type WizardPageProps } from './context';
import { NinaUrlField, SqmeterTopicField, SqmeterUrlField, SunAltitudeField } from '../settings/RoofTab';
import { CHANNEL_GROUPS } from '../settings/AlertChannelsCard';
import { useAlertsView } from '../settings/AlertsTab';
import { mergeAlertsConfig } from '../settings/defaults';
import { DepNote } from '../settings/controls';
import { ChoiceGroup, Note } from '../ui';
import { TestPanel } from './TestPanel';
import { NINA_FIELDS, WEATHER_FIELDS } from './pages';
import { t } from '../../i18n';
import { NINA_ROOF_DOC } from '../../lib/docsLinks';

// The Weather, N.I.N.A., Alerts and Daylight pages' screens (spec 015 FR-008,
// FR-009; spec 018 FR-007): each source with a Test of the device's own poll,
// the alert channel with Send test, the daylight mode.

const notSent = <Note>{t('settings.roof.notSent')}</Note>;

export const WeatherSourceScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { safety } = props.config;
  if (!safety) return notSent;
  const { http, mqtt } = safety;
  const current = http.enabled && mqtt.enabled ? 'both' : http.enabled ? 'http' : mqtt.enabled ? 'mqtt' : null;
  const choice = choiceOf(props, 'weather', current, (v) =>
    props.updateMany([
      [['safety', 'http', 'enabled'], v !== 'mqtt'],
      [['safety', 'mqtt', 'enabled'], v !== 'http'],
    ]),
  );
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'http', icon: 'link', title: t('wizard.weather.http'), detail: t('wizard.weather.httpDetail') },
          { value: 'mqtt', icon: 'wifi', title: t('wizard.weather.mqtt'), detail: t('wizard.weather.mqttDetail') },
          { value: 'both', icon: 'cloud', title: t('wizard.weather.both'), detail: t('wizard.weather.bothDetail') },
        ]}
      />
      {mqtt.enabled && <DepNote entry={props.deps.get('safety.mqtt.enabled')} onFix={props.fix} />}
    </>
  );
};

export const WeatherAddressScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { safety } = props.config;
  if (!safety) return notSent;
  return (
    <div id="weather" class="card-group">
      <div class="form-grid">
        {safety.http.enabled && <SqmeterUrlField safety={safety} update={props.update} error={props.error} />}
        {safety.mqtt.enabled && <SqmeterTopicField safety={safety} update={props.update} error={props.error} />}
      </div>
      <TestPanel part="weather" save={props.saveForTest(WEATHER_FIELDS)} dirty={props.dirty} />
    </div>
  );
};

export const NinaScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { park } = props.config;
  if (!park) return notSent;
  return (
    <div id="nina" class="card-group">
      <NinaUrlField {...props} park={park} />
      {!park.nina.url && <Note tone="bad">{t('wizard.nina.noUrl')}</Note>}
      <TestPanel part="nina" save={props.saveForTest(NINA_FIELDS)} dirty={props.dirty} />
    </div>
  );
};

export const AlpacaScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const on = props.config.alpaca?.enabled ?? false;
  const choice = choiceOf(props, 'alpaca', on ? 'on' : 'off', (v) => props.update(['alpaca', 'enabled'], v === 'on'));
  return (
    <div id="alpaca" class="card-group">
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'on', icon: 'scope', title: t('wizard.nina.on'), detail: t('wizard.nina.onDetail'), badge: t('wizard.recommended') },
          { value: 'off', icon: 'off', title: t('wizard.nina.off'), detail: t('wizard.nina.offDetail') },
        ]}
      />
      <Note>{t('wizard.nina.failsafe')}</Note>
      {choice.value === 'off' && <Note tone="warn">{t('wizard.nina.alpacaOff')}</Note>}
      <Note>
        <a href={NINA_ROOF_DOC} target="_blank" rel="noopener noreferrer">
          {t('settings.roof.ninaDocLink')}
        </a>
      </Note>
    </div>
  );
};

const CHANNELS: AlertChannelName[] = ['ntfy', 'pushover', 'webhook', 'mqtt'];

// One channel chosen here; the others go back to what the device has, so a
// change of mind doesn't leave a channel on (more can be added on the next screen).
export const AlertChannelScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const alerts = mergeAlertsConfig(props.config.alerts);
  const saved = mergeAlertsConfig(props.saved.alerts);
  // The card picked here; before a pick, the first channel that is on.
  const [picked, setPicked] = useState<AlertChannelName | null>(null);
  const current = alerts.enabled ? (picked ?? CHANNELS.find((channel) => alerts[channel].enabled) ?? null) : null;
  const choice = choiceOf(props, 'alerts', current, (v) => {
    setPicked(v as AlertChannelName);
    props.updateMany([
      [['alerts', 'enabled'], true],
      ...CHANNELS.map((channel): [string[], unknown] => [['alerts', channel, 'enabled'], channel === v || saved[channel].enabled]),
    ]);
  });
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={CHANNELS.map((channel) => ({
          value: channel,
          icon: { ntfy: 'bell', pushover: 'bell', webhook: 'link', mqtt: 'wifi' }[channel],
          title: channel === 'webhook' ? t('settings.alerts.webhook') : { ntfy: 'ntfy', pushover: 'Pushover', mqtt: 'MQTT' }[channel],
          detail: t(`wizard.alerts.${channel}`),
        }))}
      />
      {(!alerts.enabled || !current) && <Note tone="warn">{t('wizard.alerts.noChannel')}</Note>}
      {current === 'mqtt' && <DepNote entry={props.deps.get('alerts.mqtt.enabled')} onFix={props.fix} />}
    </>
  );
};

export const AlertDetailsScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { view } = useAlertsView(props);
  return (
    <div id="alerts" class="card-group">
      {CHANNELS.filter((channel) => view.alerts[channel].enabled).map((channel) => {
        const Group = CHANNEL_GROUPS[channel];
        return <Group key={channel} view={view} />;
      })}
      {view.dirty && <Note action={{ label: t('settings.save'), onClick: props.saveHere }}>{t('wizard.alerts.saveToTest')}</Note>}
    </div>
  );
};

export const DaylightScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { daylight } = props.config;
  if (!daylight) return notSent;
  const choice = choiceOf(props, 'daylight', daylight.mode, (v) => props.update(['daylight', 'mode'], v));
  return (
    <div id="daylight" class="card-group">
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'off', icon: 'moon', title: t('wizard.daylight.offTitle'), detail: t('wizard.daylight.off') },
          {
            value: 'alert',
            icon: 'sun',
            title: t('wizard.daylight.alertTitle'),
            detail: t('wizard.daylight.alert'),
            badge: t('wizard.recommended'),
          },
          { value: 'close', icon: 'roof', title: t('wizard.daylight.closeTitle'), detail: t('wizard.daylight.close') },
        ]}
      />
      {choice.value && daylight.mode !== 'off' && (
        <div class="form-grid">
          <SunAltitudeField daylight={daylight} update={props.update} error={props.error} />
        </div>
      )}
      <DepNote entry={props.deps.get('daylight.mode')} onFix={props.fix} />
      {daylight.mode !== 'off' && !props.config.location?.set && <Note tone="warn">{t('wizard.daylight.needsSite')}</Note>}
    </div>
  );
};
