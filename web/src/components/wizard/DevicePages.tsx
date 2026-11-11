import { FunctionalComponent } from 'preact';
import { useEffect, useRef } from 'preact/hooks';
import type { WizardPageProps } from './context';
import { DeviceNameField, PasswordFields } from '../settings/DeviceTab';
import { NtpCard, TimeZoneCard } from '../settings/TimeTab';
import { LocationFields } from '../settings/RoofTab';
import { SettingsCard } from '../settings/controls';
import { Note } from '../ui';
import { t } from '../../i18n';

// Pages 1 and 2 (spec 015 FR-008): the device and its password; the site and the clock.

// A password is the strong recommendation (constitution VIII): protection
// starts switched on when it is off and none is set; turning it off warns.
export const DevicePage: FunctionalComponent<WizardPageProps> = (props) => {
  const { config, updateMany } = props;
  const offered = useRef(false);
  useEffect(() => {
    if (offered.current) return;
    offered.current = true;
    if (!config.auth.enabled && config.auth.password === '') updateMany([[['auth', 'enabled'], true]]);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- once, when the page opens: never again after the owner turns it off
  }, []);
  return (
    <>
      <SettingsCard title={t('wizard.device.title')} hint={t('wizard.device.hint')}>
        <DeviceNameField {...props} />
      </SettingsCard>
      <SettingsCard id="security" title={t('wizard.device.password')} hint={t('wizard.device.passwordHint')}>
        <PasswordFields {...props} />
        {config.auth.enabled ? (
          <Note>{t('wizard.device.passwordNeeded')}</Note>
        ) : (
          <Note tone="bad">{t('wizard.device.noPasswordWarning')}</Note>
        )}
      </SettingsCard>
    </>
  );
};

export const SitePage: FunctionalComponent<WizardPageProps> = (props) => (
  <>
    {props.config.location && (
      <SettingsCard id="site" title={t('wizard.site.title')} hint={t('wizard.site.hint')}>
        <LocationFields location={props.config.location} update={props.update} error={props.error} />
        {!props.config.location.set && <Note tone="warn">{t('wizard.site.notSet')}</Note>}
      </SettingsCard>
    )}
    <TimeZoneCard {...props} />
    <NtpCard {...props} />
  </>
);
