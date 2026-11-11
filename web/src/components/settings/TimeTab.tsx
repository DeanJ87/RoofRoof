import { FunctionalComponent } from 'preact';
import type { SettingsTabProps } from './context';
import { DepToggle, Field, NumberInput, SelectInput, SettingsCard, TextInput } from './controls';
import { t } from '../../i18n';

// Common time zones in POSIX TZ format
export const TIMEZONE_OPTIONS = [
  { label: 'UTC', value: 'UTC0' },
  { label: t('settings.time.gmtNoDaylightSaving'), value: 'GMT0' },
  { label: t('settings.time.usPacificPst'), value: 'PST8PDT,M3.2.0,M11.1.0' },
  { label: t('settings.time.usMountainMst'), value: 'MST7MDT,M3.2.0,M11.1.0' },
  { label: t('settings.time.usCentralCst'), value: 'CST6CDT,M3.2.0,M11.1.0' },
  { label: t('settings.time.usEasternEst'), value: 'EST5EDT,M3.2.0,M11.1.0' },
  { label: t('settings.time.europeLondonGmt'), value: 'GMT0BST,M3.5.0/1,M10.5.0' },
  { label: t('settings.time.europeParisCet'), value: 'CET-1CEST,M3.5.0,M10.5.0/3' },
  { label: t('settings.time.australiaSydneyAest'), value: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
  { label: t('settings.time.asiaTokyoJst'), value: 'JST-9' },
];

const knownZone = (timezone: string) => TIMEZONE_OPTIONS.some((tz) => tz.value === timezone);

export const TimeZoneCard: FunctionalComponent<SettingsTabProps> = ({ config, update, error }) => (
  <SettingsCard title={t('settings.time.timeZone')}>
    <div class="form-grid">
      <Field label={t('settings.time.timeZone')}>
        <SelectInput
          dataField="ntp.timezone"
          value={knownZone(config.ntp.timezone) ? config.ntp.timezone : 'custom'}
          options={[...TIMEZONE_OPTIONS, { value: 'custom', label: t('common.custom') }]}
          onChange={(v) => update(['ntp', 'timezone'], v === 'custom' ? '' : v)}
        />
      </Field>
      {!knownZone(config.ntp.timezone) && (
        <Field label={t('settings.time.posixTimeZone')} hint={t('settings.time.eGPst8pdtM32')} error={error('ntp.timezone')}>
          <TextInput dataField="ntp.timezone" value={config.ntp.timezone} onInput={(v) => update(['ntp', 'timezone'], v)} />
        </Field>
      )}
    </div>
  </SettingsCard>
);

// NTP is the device's only clock source (spec 001 research R11).
export const NtpCard: FunctionalComponent<SettingsTabProps> = ({ config, update, error, deps, fix }) => (
  <SettingsCard id="ntp" title="NTP">
    <DepToggle
      entry={deps.get('ntp.enabled')}
      onFix={fix}
      label={t('settings.time.internetTimeNtp')}
      checked={config.ntp.enabled}
      onChange={(v) => update(['ntp', 'enabled'], v)}
    />
    {config.ntp.enabled && (
      <div class="form-grid">
        <Field label={t('settings.time.server')} error={error('ntp.server1')}>
          <TextInput
            dataField="ntp.server1"
            value={config.ntp.server1}
            placeholder="pool.ntp.org"
            onInput={(v) => update(['ntp', 'server1'], v)}
          />
        </Field>
        <Field label={t('settings.time.fallbackServer')}>
          <TextInput
            dataField="ntp.server2"
            value={config.ntp.server2}
            placeholder="time.nist.gov"
            onInput={(v) => update(['ntp', 'server2'], v)}
          />
        </Field>
        <Field label={t('settings.time.syncEvery')} error={error('ntp.syncIntervalMs')}>
          <NumberInput
            dataField="ntp.syncIntervalMs"
            integer
            min={10}
            max={1440}
            unit="min"
            value={config.ntp.syncIntervalMs / 60000}
            onChange={(v) => update(['ntp', 'syncIntervalMs'], v * 60000)}
          />
        </Field>
      </div>
    )}
  </SettingsCard>
);

const TimeTab: FunctionalComponent<SettingsTabProps> = (props) => (
  <>
    <TimeZoneCard {...props} />

    <NtpCard {...props} />
  </>
);

export default TimeTab;
