import { FunctionalComponent } from 'preact';
import type { SettingsTabProps } from './context';
import { DepToggle, Field, SettingsCard, TextInput, Toggle } from './controls';
import { t } from '../../i18n';
import BackupCard from './BackupCard';

// Password protection (also the setup wizard's first page, spec 015).
export const PasswordFields: FunctionalComponent<Pick<SettingsTabProps, 'config' | 'update' | 'error'>> = ({ config, update, error }) => {
  const auth = config.auth ?? { enabled: false, username: 'admin', password: '' };
  return (
    <>
      <Toggle
        dataField="auth.enabled"
        label={t('settings.device.passwordProtectChanges')}
        checked={auth.enabled}
        onChange={(v) => update(['auth', 'enabled'], v)}
        hint={t('settings.device.requiredToChangeSettingsUpdate')}
      />
      {auth.enabled && (
        <div class="form-grid indent">
          <Field label={t('settings.device.username')} error={error('auth.username')}>
            <TextInput dataField="auth.username" value={auth.username} onInput={(v) => update(['auth', 'username'], v)} />
          </Field>
          <Field label={t('settings.device.password')} error={error('auth.password')} hint={t('settings.device.leaveTheMaskToKeep')}>
            <TextInput dataField="auth.password" type="password" value={auth.password} onInput={(v) => update(['auth', 'password'], v)} />
          </Field>
        </div>
      )}
    </>
  );
};

// The device's name (shown in N.I.N.A.).
export const DeviceNameField: FunctionalComponent<Pick<SettingsTabProps, 'config' | 'update' | 'error'>> = ({ config, update, error }) => (
  <Field label={t('settings.device.name')} error={error('deviceName')} hint={t('settings.device.shownInNIN')}>
    <TextInput dataField="deviceName" value={config.deviceName} onInput={(v) => update(['deviceName'], v)} />
  </Field>
);

// Password protection and command-line (ArduinoOTA) uploads.
const SecurityCard: FunctionalComponent<SettingsTabProps> = ({ config, update, error, deps, fix }) => {
  return (
    <SettingsCard id="security" title={t('settings.device.security')}>
      <PasswordFields config={config} update={update} error={error} />
      <DepToggle
        entry={deps.get('ota.enabled')}
        onFix={fix}
        label={t('settings.device.commandLineUploadsArduinoota')}
        checked={config.ota.enabled}
        onChange={(v) => update(['ota', 'enabled'], v)}
        hint={t('settings.device.forPioRunTUpload')}
      />
      {config.ota.enabled && (
        <div class="form-grid indent">
          <Field label={t('settings.device.uploadPassword')} error={error('ota.password') ?? error('otaPassword')}>
            <TextInput
              dataField="ota.password"
              type="password"
              value={config.ota.password}
              onInput={(v) => update(['ota', 'password'], v)}
            />
          </Field>
        </div>
      )}
    </SettingsCard>
  );
};

const DeviceTab: FunctionalComponent<SettingsTabProps> = (props) => {
  const { config, update, error } = props;
  return (
    <>
      <SettingsCard title={t('settings.device.device')}>
        <DeviceNameField config={config} update={update} error={error} />
      </SettingsCard>

      <SecurityCard {...props} />

      <BackupCard deviceName={config.deviceName} status={props.status} />
    </>
  );
};

export default DeviceTab;
