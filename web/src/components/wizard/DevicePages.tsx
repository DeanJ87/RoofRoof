import { FunctionalComponent } from 'preact';
import type { Config } from '../../types';
import { choiceOf, mqttChange, type WizardPageProps } from './context';
import { DeviceNameField, PasswordInputs } from '../settings/DeviceTab';
import { NtpFields, TimeZoneFields } from '../settings/TimeTab';
import { LocationFields, SqmeterTopicField, SqmeterUrlField } from '../settings/RoofTab';
import { MqttFields } from '../settings/NetworkTab';
import SqmeterCopy from '../settings/SqmeterCopy';
import { Toggle } from '../settings/controls';
import { locationSource } from '../../lib/sqmeterCopy';
import { locked } from '../../lib/syncView';
import { ChoiceGroup, Note } from '../ui';
import { t } from '../../i18n';

// The name, SQMeter, site and time, and password screens (specs 015, 018, 020, 025).

const notSent = <Note>{t('settings.roof.notSent')}</Note>;

export const NameScreen: FunctionalComponent<WizardPageProps> = (props) => <DeviceNameField {...props} />;

// A password is the strong recommendation (constitution VIII). Nothing is chosen
// for the person (spec 025): the screen never changes a setting when it opens,
// so a saved "No password" stays chosen on every later visit.
export const PasswordScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const choice = choiceOf(props, 'password', props.config.auth.enabled ? 'on' : 'off', (v) =>
    props.update(['auth', 'enabled'], v === 'on'),
  );
  return (
    <>
      <ChoiceGroup
        {...choice}
        options={[
          { value: 'on', icon: 'lock', title: t('wizard.device.on'), detail: t('wizard.device.onDetail'), badge: t('wizard.recommended') },
          { value: 'off', icon: 'off', title: t('wizard.device.off'), detail: t('wizard.device.offDetail') },
        ]}
      />
      {choice.value === 'on' && <PasswordInputs {...props} />}
      {choice.value === 'off' && <Note tone="bad">{t('wizard.device.noPasswordWarning')}</Note>}
    </>
  );
};

/** The location is read from SQMeter (spec 020). */
const locationFromSqmeter = (config: Config) =>
  Boolean(config.location && locationSource(config.location) === 'sqmeter' && config.safety?.http.url.trim());

/** Both the location and the time follow SQMeter: the site and time screen isn't asked. */
export const siteFromSqmeter = (config: Config): boolean => locationFromSqmeter(config) && locked(config, 'time');

// What SQMeter answers for the site and time, each with Edit (FR-018).
const SqmeterDone: FunctionalComponent<WizardPageProps> = (props) => {
  const edit = { label: t('wizard.sqmeter.edit'), onClick: () => void props.goToScreen('site') };
  return (
    <>
      {locationFromSqmeter(props.config) && (
        <Note tone="ok" id="sqmeter-done-location" action={edit}>
          {t('wizard.sqmeter.locationDone')}
        </Note>
      )}
      {locked(props.config, 'time') && (
        <Note tone="ok" id="sqmeter-done-time" action={edit}>
          {t('wizard.sqmeter.timeDone')}
        </Note>
      )}
    </>
  );
};

// The weather verdict from SQMeter, over its web address and/or MQTT, with MQTT's
// broker here when MQTT is off (FR-007). A test box's weather bypass can't be set
// with an SQMeter source (spec 022), so the screen only says it's bypassed.
const SqmeterWeather: FunctionalComponent<WizardPageProps> = (props) => {
  const { safety, testBox } = props.config;
  if (!safety) return null;
  if (testBox?.weather) return <Note tone="warn">{t('wizard.sqmeter.weatherBypassed')}</Note>;
  return (
    <>
      <Toggle
        dataField="safety.http.enabled"
        label={t('wizard.sqmeter.weather')}
        hint={t('wizard.sqmeter.weatherHint')}
        checked={safety.http.enabled}
        onChange={(v) => props.update(['safety', 'http', 'enabled'], v)}
      />
      <Toggle
        dataField="safety.mqtt.enabled"
        label={t('settings.roof.sqmeterMqtt')}
        hint={t('settings.roof.sqmeterMqttHint')}
        checked={safety.mqtt.enabled}
        onChange={(v) =>
          props.updateMany([[['safety', 'mqtt', 'enabled'], v], mqttChange(props, v, Boolean(props.config.alerts?.mqtt.enabled))])
        }
      />
      {safety.mqtt.enabled && (
        <div class="form-grid indent">
          <SqmeterTopicField safety={safety} update={props.update} error={props.error} config={props.config} status={props.status} />
        </div>
      )}
      {safety.mqtt.enabled && !props.saved.mqtt.enabled && <MqttFields {...props} />}
    </>
  );
};

// Spec 020 and 025: SQMeter's address and login once, its Test, the copy of its
// site settings (applied to the form; Continue saves them) and the weather verdict.
export const SqmeterScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { safety } = props.config;
  if (!safety) return notSent;
  return (
    <div id="sqmeter" class="card-group">
      <div class="form-grid">
        <SqmeterUrlField safety={safety} update={props.update} error={props.error} />
      </div>
      <SqmeterCopy {...props} url={safety.http.url} wizard />
      <SqmeterDone {...props} />
      <SqmeterWeather {...props} />
    </div>
  );
};

// The location and the time, when SQMeter doesn't supply them both (FR-018).
export const SiteScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { location } = props.config;
  if (!location) return notSent;
  return (
    <>
      <LocationFields
        location={location}
        update={props.update}
        error={props.error}
        status={props.status}
        sqmeterUrl={props.config.safety?.http.url ?? ''}
        onSetAddress={() => void props.goToScreen('sqmeter')}
      />
      {locationSource(location) === 'manual' && !location.set && <Note tone="warn">{t('wizard.site.notSet')}</Note>}
      <TimeZoneFields {...props} />
      <NtpFields {...props} followNote={false} />
    </>
  );
};
