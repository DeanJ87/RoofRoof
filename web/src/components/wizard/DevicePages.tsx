import { FunctionalComponent } from 'preact';
import { useLayoutEffect } from 'preact/hooks';
import { choiceOf, type WizardPageProps } from './context';
import { DeviceNameField, PasswordInputs } from '../settings/DeviceTab';
import { NtpFields, TimeZoneFields } from '../settings/TimeTab';
import { LocationFields, SqmeterUrlField } from '../settings/RoofTab';
import SqmeterCopy from '../settings/SqmeterCopy';
import { locationSource } from '../../lib/sqmeterCopy';
import { pageIndex } from './pages';
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

// Spec 020: SQMeter's address first, and a copy of its site settings (applied to
// the form; Continue saves them). An empty address means no SQMeter.
export const SqmeterScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { safety } = props.config;
  if (!safety) return <Note>{t('settings.roof.notSent')}</Note>;
  return (
    <div id="sqmeter" class="card-group">
      <div class="form-grid">
        <SqmeterUrlField safety={safety} update={props.update} error={props.error} />
      </div>
      <SqmeterCopy {...props} url={safety.http.url} wizard />
    </div>
  );
};

export const SiteScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { location } = props.config;
  if (!location) return <Note>{t('settings.roof.notSent')}</Note>;
  return (
    <>
      <LocationFields
        location={location}
        update={props.update}
        error={props.error}
        status={props.status}
        sqmeterUrl={props.config.safety?.http.url ?? ''}
        onSetAddress={() => void props.goToPage(pageIndex('site'))}
      />
      {locationSource(location) === 'manual' && !location.set && <Note tone="warn">{t('wizard.site.notSet')}</Note>}
    </>
  );
};

export const TimeScreen: FunctionalComponent<WizardPageProps> = (props) => (
  <>
    <TimeZoneFields {...props} />
    <NtpFields {...props} />
  </>
);
