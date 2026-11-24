import { FunctionalComponent } from 'preact';
import type { AlertChannelName } from '../../types';
import { choiceOf, mqttChange, type WizardPageProps } from './context';
import { NinaUrlField, SunAltitudeField } from '../settings/RoofTab';
import { MqttFields } from '../settings/NetworkTab';
import { CHANNEL_GROUPS } from '../settings/AlertChannelsCard';
import { useAlertsView } from '../settings/AlertsTab';
import { mergeAlertsConfig } from '../settings/defaults';
import { DepNote, Group, Toggle } from '../settings/controls';
import { ChoiceGroup, Note } from '../ui';
import { TestPanel } from './TestPanel';
import { NINA_FIELDS, pageIndex } from './pages';
import { ninaCounts } from '../../lib/parkSource';
import { t } from '../../i18n';
import { NINA_ROOF_DOC } from '../../lib/docsLinks';
import { locationSource } from '../../lib/sqmeterCopy';

// The N.I.N.A., alerts and daylight screens (specs 015, 018, 025): N.I.N.A.
// with a Test of the device's own poll and Alpaca, every alert channel with its
// fields and Send test, the daylight mode.

const notSent = <Note>{t('settings.roof.notSent')}</Note>;

// N.I.N.A.'s address with its Test, and whether its sequence drives the roof over Alpaca (FR-020).
export const NinaScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { park } = props.config;
  if (!park) return notSent;
  const alpaca = props.config.alpaca?.enabled ?? false;
  return (
    <div id="nina" class="card-group">
      <Note tone="warn">{t('settings.roof.liveNinaWarning')}</Note>
      <Note action={{ label: t('wizard.nina.useTestBox'), onClick: () => void props.goToPage(pageIndex('testBox')) }}>
        {t('wizard.nina.noTelescope')}
      </Note>
      <NinaUrlField {...props} park={park} />
      {/* Spec 024: with park switches only the address is optional. */}
      {!park.nina.url && ninaCounts(park) && <Note tone="bad">{t('wizard.nina.noUrl')}</Note>}
      {!ninaCounts(park) && <Note>{t('settings.roof.ninaOptional')}</Note>}
      <TestPanel save={props.saveForTest(NINA_FIELDS)} dirty={props.dirty} />
      <Toggle
        dataField="alpaca.enabled"
        label={t('wizard.nina.alpaca')}
        hint={t('wizard.nina.failsafe')}
        checked={alpaca}
        onChange={(v) => props.update(['alpaca', 'enabled'], v)}
      />
      {!alpaca && <Note tone="warn">{t('wizard.nina.alpacaOff')}</Note>}
      <Note>
        <a href={NINA_ROOF_DOC} target="_blank" rel="noopener noreferrer">
          {t('settings.roof.ninaDocLink')}
        </a>
      </Note>
    </div>
  );
};

const CHANNELS: AlertChannelName[] = ['ntfy', 'pushover', 'webhook', 'mqtt'];
const CHANNEL_NAME: Record<AlertChannelName, () => string> = {
  ntfy: () => 'ntfy',
  pushover: () => 'Pushover',
  webhook: () => t('settings.alerts.webhook'),
  mqtt: () => 'MQTT',
};
const CHANNEL_ICON: Record<AlertChannelName | 'none', string> = {
  ntfy: 'bell',
  pushover: 'bell',
  webhook: 'link',
  mqtt: 'wifi',
  none: 'off',
};

/** The alert channels switched on in the settings as edited. */
export const alertChannelsOn = (props: Pick<WizardPageProps, 'config'>): AlertChannelName[] => {
  const alerts = mergeAlertsConfig(props.config.alerts);
  return alerts.enabled ? CHANNELS.filter((channel) => alerts[channel].enabled) : [];
};

// The changes a tick makes: a channel on or off (alerts on with it; MQTT with
// the MQTT channel, FR-022), or "No alerts for now", which turns every channel off.
const tickChanges = (props: WizardPageProps, value: string): [string[], unknown][] => {
  const weatherMqtt = Boolean(props.config.safety?.mqtt.enabled);
  const now = alertChannelsOn(props);
  const channel = value as AlertChannelName;
  const on = value !== 'none' && !now.includes(channel);
  const next = value === 'none' ? [] : on ? [...now, channel] : now.filter((c) => c !== channel);
  // Every channel set as ticked (one left on while alerts were off doesn't come back), alerts on with any.
  return [
    [['alerts', 'enabled'], next.length > 0],
    ...CHANNELS.map((c): [string[], unknown] => [['alerts', c, 'enabled'], next.includes(c)]),
    mqttChange(props, next.includes('mqtt'), weatherMqtt),
  ];
};

// Spec 025 (FR-021, FR-022): several channels or none, as in Settings, each
// ticked channel's fields right here (prefilled where copied from SQMeter).
export const AlertsScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { view } = useAlertsView(props);
  const on = alertChannelsOn(props);
  const values = on.length > 0 ? on : props.chosen ? ['none'] : [];
  return (
    <div id="alerts" class="card-group">
      <ChoiceGroup
        name="alerts"
        labelledBy={props.questionId}
        value={null}
        multiple
        values={values}
        onChange={(value) => {
          props.updateMany(tickChanges(props, value));
          props.choose();
        }}
        options={[
          ...CHANNELS.map((channel) => ({
            value: channel,
            icon: CHANNEL_ICON[channel],
            title: CHANNEL_NAME[channel](),
            detail: t(`wizard.alerts.${channel}`),
          })),
          { value: 'none', icon: CHANNEL_ICON.none, title: t('wizard.alerts.none'), detail: t('wizard.alerts.noneDetail') },
        ]}
      />
      {values.includes('none') && <Note tone="warn">{t('wizard.alerts.noChannel')}</Note>}
      {on.map((channel) => {
        const Channel = CHANNEL_GROUPS[channel];
        return (
          <Group key={channel} title={CHANNEL_NAME[channel]()}>
            {channel === 'mqtt' && !props.saved.mqtt.enabled && <MqttFields {...props} />}
            <Channel view={view} toggle={false} />
          </Group>
        );
      })}
      {on.length > 0 && view.dirty && (
        <Note action={{ label: t('settings.save'), onClick: props.saveHere }}>{t('wizard.alerts.saveToTest')}</Note>
      )}
    </div>
  );
};

// Spec 020: a location not set yet that the device reads from SQMeter once saved.
const fromSqmeter = (props: WizardPageProps) => {
  const { location, safety } = props.config;
  return Boolean(location && locationSource(location) === 'sqmeter' && safety?.http.url.trim());
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
      {props.config.location?.set && <DepNote entry={props.deps.get('daylight.mode')} onFix={props.fix} />}
      {daylight.mode !== 'off' && !props.config.location?.set && fromSqmeter(props) && <Note>{t('wizard.daylight.fromSqmeter')}</Note>}
      {daylight.mode !== 'off' && !props.config.location?.set && !fromSqmeter(props) && (
        <Note tone="warn" action={{ label: t('wizard.daylight.setSite'), onClick: () => void props.goToScreen('site') }}>
          {t('wizard.daylight.needsSite')}
        </Note>
      )}
    </div>
  );
};
