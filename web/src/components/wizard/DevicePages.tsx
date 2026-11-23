import { FunctionalComponent } from 'preact';
import { useLayoutEffect } from 'preact/hooks';
import { choiceOf, type WizardPageProps } from './context';
import { DeviceNameField, PasswordInputs } from '../settings/DeviceTab';
import { NtpFields, TimeZoneFields } from '../settings/TimeTab';
import { LocationFields } from '../settings/RoofTab';
import { ChoiceGroup, Note } from '../ui';
import { t } from '../../i18n';

// The Device and Site and time pages' screens (spec 015 FR-008, spec 018 FR-007).

export const NameScreen: FunctionalComponent<WizardPageProps> = (props) => <DeviceNameField {...props} />;

// A password is the strong recommendation (constitution VIII): "Set a
// password" starts chosen when protection is off and none is set (spec 015);
// "No password" warns.
export const PasswordScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { config, updateMany } = props;
  // In the commit, before anything can be pressed: a later offer would undo the owner's choice.
  useLayoutEffect(() => {
    if (!props.offerPassword()) return;
    updateMany([[['auth', 'enabled'], true]]);
    props.choose();
    // eslint-disable-next-line react-hooks/exhaustive-deps -- once per wizard visit (offerPassword says so): never again after the owner turns it off
  }, []);
  const on = config.auth.enabled;
  const choice = choiceOf(props, 'password', on ? 'on' : 'off', (v) => props.update(['auth', 'enabled'], v === 'on'));
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
      {choice.value === 'on' && <Note>{t('wizard.device.passwordNeeded')}</Note>}
      {choice.value === 'off' && <Note tone="bad">{t('wizard.device.noPasswordWarning')}</Note>}
    </>
  );
};

export const SiteScreen: FunctionalComponent<WizardPageProps> = (props) =>
  props.config.location ? (
    <>
      <LocationFields location={props.config.location} update={props.update} error={props.error} />
      {!props.config.location.set && <Note tone="warn">{t('wizard.site.notSet')}</Note>}
    </>
  ) : (
    <Note>{t('settings.roof.notSent')}</Note>
  );

export const TimeScreen: FunctionalComponent<WizardPageProps> = (props) => (
  <>
    <TimeZoneFields {...props} />
    <NtpFields {...props} />
  </>
);
