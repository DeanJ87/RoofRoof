import { FunctionalComponent } from 'preact';
import type { SafetyConfig } from '../../types';
import type { SettingsTabProps } from './context';
import { Field, TextInput } from './controls';
import { t } from '../../i18n';

// Spec 020: SQMeter's login (HTTP Basic), next to its address wherever the
// address is edited. The device sends it on every SQMeter request; the saved
// password shows as the mask, and saving another SQMeter's address clears it.
export const SqmeterLoginFields: FunctionalComponent<{ safety: SafetyConfig } & Pick<SettingsTabProps, 'update' | 'error'>> = ({
  safety,
  update,
  error,
}) => (
  <>
    <Field label={t('settings.roof.sqmeterUser')} hint={t('settings.roof.sqmeterLoginHint')} error={error('safety.http.username')}>
      <TextInput
        dataField="safety.http.username"
        value={safety.http.username ?? ''}
        placeholder={t('settings.network.optional')}
        onInput={(v) => update(['safety', 'http', 'username'], v)}
      />
    </Field>
    <Field label={t('settings.roof.sqmeterPassword')} hint={t('settings.roof.sqmeterPasswordHint')} error={error('safety.http.password')}>
      <TextInput
        dataField="safety.http.password"
        type="password"
        value={safety.http.password ?? ''}
        placeholder={t('settings.network.optional')}
        onInput={(v) => update(['safety', 'http', 'password'], v)}
      />
    </Field>
  </>
);
