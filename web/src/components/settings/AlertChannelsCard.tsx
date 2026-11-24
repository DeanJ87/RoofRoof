import { FunctionalComponent } from 'preact';
import type { AlertChannelName } from '../../types';
import { isPending, soundOptions, type AlertsView } from './alertsShared';
import { AlertTestNote } from './AlertTemplateEditor';
import { ActionButton, DepToggle, Field, Group, SelectInput, SettingsCard, TextInput, Toggle } from './controls';
import { FollowsSqmeter, NeedsNote } from './FollowsSqmeter';
import { asked, ntfyShown } from '../../lib/syncView';
import { t } from '../../i18n';

// `toggle` false: the group without its on switch (the setup wizard ticks channels itself, spec 025).
type GroupProps = { view: AlertsView; toggle?: boolean };

// Send test, for a channel that is on (tests use the saved settings).
const TestButton: FunctionalComponent<{ view: AlertsView; channel: AlertChannelName }> = ({ view, channel }) => {
  const entry = view.dep(`alerts.${channel}.enabled`);
  if (entry.state === 'off' || entry.state === 'inactive') return null;
  return (
    <div class="btn-row">
      <ActionButton
        onClick={() => view.tester.sendTest(channel)}
        disabled={view.dirty || isPending(view.tester.testResult)}
        title={view.dirty ? t('settings.alerts.saveFirst') : undefined}
      >
        {t('settings.alerts.sendTest')}
      </ActionButton>
      <AlertTestNote view={view} target={channel} />
    </div>
  );
};

const PushoverGroup: FunctionalComponent<GroupProps> = ({ view, toggle = true }) => {
  const { alerts, set, err, dep, fix, status } = view;
  return (
    <Group>
      {toggle && (
        <DepToggle
          entry={dep('alerts.pushover.enabled')}
          onFix={fix}
          label="Pushover"
          checked={alerts.pushover.enabled}
          onChange={(v) => set(['pushover', 'enabled'], v)}
        />
      )}
      {/* Spec 020: also while off when SQMeter has keys RoofRoof lacks, so they can be entered. */}
      {(alerts.pushover.enabled || asked(status?.sync, 'pushoverKeys')) && (
        <>
          <div class="form-grid">
            <Field label={t('settings.alerts.userKey')} error={err('pushover.userKey')} hint={t('settings.alerts.yourUserKeyTopOf')}>
              <TextInput
                dataField="alerts.pushover.userKey"
                type="password"
                value={alerts.pushover.userKey}
                onInput={(v) => set(['pushover', 'userKey'], v)}
              />
            </Field>
            <Field
              label={t('settings.alerts.appToken')}
              error={err('pushover.appToken')}
              hint={t('settings.alerts.createAnApplicationAtPushover')}
            >
              <TextInput
                dataField="alerts.pushover.appToken"
                type="password"
                value={alerts.pushover.appToken}
                onInput={(v) => set(['pushover', 'appToken'], v)}
              />
              <NeedsNote status={status} need="pushoverKeys" filled={!!alerts.pushover.userKey && !!alerts.pushover.appToken} />
            </Field>
            <Field label={t('settings.alerts.defaultSound')} hint={t('settings.alerts.usedWhereAnEventS')}>
              <SelectInput
                value={alerts.pushover.sound}
                options={soundOptions(alerts.pushover.sound, t('settings.alerts.pushoverDefault'))}
                onChange={(v) => set(['pushover', 'sound'], v)}
              />
            </Field>
          </div>
          <TestButton view={view} channel="pushover" />
        </>
      )}
    </Group>
  );
};

// Spec 020: the server and topic follow SQMeter while the group does; spec 026: typing here unfollows it.
const NtfyGroup: FunctionalComponent<GroupProps> = ({ view, toggle = true }) => {
  const { alerts, set, err, dep, fix, config, status, update } = view;
  return (
    <Group>
      {toggle && (
        <DepToggle
          entry={dep('alerts.ntfy.enabled')}
          onFix={fix}
          label="ntfy"
          checked={alerts.ntfy.enabled}
          onChange={(v) => set(['ntfy', 'enabled'], v)}
        />
      )}
      {/* Spec 020: also while off when it holds what was copied or synced from SQMeter, or a token to enter. */}
      {ntfyShown(config, status?.sync) && (
        <>
          <FollowsSqmeter group="alerts" config={config} status={status} update={update} />
          <div class="form-grid">
            <Field label={t('settings.alerts.server')} error={err('ntfy.server')}>
              <TextInput type="url" dataField="alerts.ntfy.server" value={alerts.ntfy.server} onInput={(v) => set(['ntfy', 'server'], v)} />
            </Field>
            <Field label={t('settings.alerts.topic')} error={err('ntfy.topic')} hint={t('settings.alerts.ntfyTopicsPublic')}>
              <TextInput dataField="alerts.ntfy.topic" value={alerts.ntfy.topic} onInput={(v) => set(['ntfy', 'topic'], v)} />
            </Field>
            <Field label={t('settings.alerts.token')}>
              <TextInput
                type="password"
                dataField="alerts.ntfy.token"
                value={alerts.ntfy.token ?? ''}
                placeholder={t('settings.alerts.optional')}
                onInput={(v) => set(['ntfy', 'token'], v)}
              />
              <NeedsNote status={status} need="ntfyToken" filled={!!alerts.ntfy.token} cleared={alerts.ntfy.token === null} />
            </Field>
          </div>
          <TestButton view={view} channel="ntfy" />
        </>
      )}
    </Group>
  );
};

const WebhookGroup: FunctionalComponent<GroupProps> = ({ view, toggle = true }) => {
  const { alerts, set, err, dep, fix } = view;
  return (
    <Group>
      {toggle && (
        <DepToggle
          entry={dep('alerts.webhook.enabled')}
          onFix={fix}
          label={t('settings.alerts.webhook')}
          checked={alerts.webhook.enabled}
          onChange={(v) => set(['webhook', 'enabled'], v)}
          hint={t('settings.alerts.postsJsonDeviceEventTitle')}
        />
      )}
      {alerts.webhook.enabled && (
        <>
          <div class="form-grid">
            <Field label="URL" error={err('webhook.url')}>
              <TextInput
                dataField="alerts.webhook.url"
                type="url"
                value={alerts.webhook.url}
                placeholder="http://homeassistant.local:8123/api/webhook/..."
                onInput={(v) => set(['webhook', 'url'], v)}
              />
            </Field>
            <Field label={t('settings.alerts.authorizationHeader')}>
              <TextInput
                type="password"
                value={alerts.webhook.authHeader}
                placeholder={t('settings.alerts.optional')}
                onInput={(v) => set(['webhook', 'authHeader'], v)}
              />
            </Field>
          </div>
          {alerts.webhook.url.startsWith('https://') && (
            <Toggle
              label={t('settings.alerts.skipCertificateChecks')}
              checked={alerts.webhook.insecureTls}
              onChange={(v) => set(['webhook', 'insecureTls'], v)}
              hint={t('settings.alerts.onlyForASelfSigned')}
            />
          )}
          <TestButton view={view} channel="webhook" />
        </>
      )}
    </Group>
  );
};

const MqttGroup: FunctionalComponent<GroupProps> = ({ view, toggle = true }) => (
  <Group>
    {toggle && (
      <DepToggle
        entry={view.dep('alerts.mqtt.enabled')}
        onFix={view.fix}
        label="MQTT"
        checked={view.alerts.mqtt.enabled}
        onChange={(v) => view.set(['mqtt', 'enabled'], v)}
        hint={t('settings.alerts.publishesEachAlertToTopic', { topic: view.config.mqtt.topic })}
      />
    )}
    <TestButton view={view} channel="mqtt" />
  </Group>
);

// Each channel's group (also the setup wizard's alerts screen, spec 025).
export const CHANNEL_GROUPS: Record<AlertChannelName, FunctionalComponent<GroupProps>> = {
  pushover: PushoverGroup,
  ntfy: NtfyGroup,
  webhook: WebhookGroup,
  mqtt: MqttGroup,
};

// Where alerts go: Pushover, ntfy, a webhook and MQTT.
export const AlertChannelsCard: FunctionalComponent<{ view: AlertsView }> = ({ view }) => (
  <SettingsCard title={t('settings.alerts.channels')} hint={t('settings.alerts.testsUseTheSavedSettings')}>
    <PushoverGroup view={view} />
    <NtfyGroup view={view} />
    <WebhookGroup view={view} />
    <MqttGroup view={view} />
  </SettingsCard>
);
