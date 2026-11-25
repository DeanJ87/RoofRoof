import { FunctionalComponent } from 'preact';
import type { LocationConfig, LocationStatus } from '../../types';
import type { SettingsTabProps } from './context';
import { Field, NumberInput, SelectInput, Toggle } from './controls';
import { Note, ReadingRow } from '../ui';
import { locationSource } from '../../lib/sqmeterCopy';
import { locationLine } from '../../lib/locationView';
import { formatCoordinates } from '../../i18n/format';
import { t } from '../../i18n';

// A degrees field that can be negative (the sun's altitude, latitude, longitude).
export const DegreesField: FunctionalComponent<{
  at: string[];
  label: string;
  hint: string;
  value: number;
  update: SettingsTabProps['update'];
  error: SettingsTabProps['error'];
}> = ({ at, label, hint, value, update, error }) => (
  <Field label={label} hint={hint} error={error(at.join('.'))}>
    <NumberInput dataField={at.join('.')} value={value} unit="°" onChange={(v) => update(at, v)} />
  </Field>
);

// Where the roof is (also the setup wizard's site page). Spec 020: from
// SQMeter (kept by the device, shown read-only) or typed in.
type LocationProps = Pick<SettingsTabProps, 'update' | 'error' | 'status'> & {
  location: LocationConfig;
  sqmeterUrl: string; // SQMeter's address as edited
  onSetAddress?: () => void; // go to where SQMeter's address is set
};

export const LocationFields: FunctionalComponent<LocationProps> = (props) => {
  const { location, update } = props;
  const source = locationSource(location);
  return (
    <>
      <div class="form-grid">
        <Field label={t('settings.roof.locationSource')} hint={t('settings.roof.locationSourceHint')}>
          <SelectInput
            dataField="location.source"
            value={source}
            options={[
              { value: 'sqmeter', label: t('settings.roof.locationFromSqmeter') },
              { value: 'manual', label: t('settings.roof.locationTyped') },
            ]}
            onChange={(v) => update(['location', 'source'], v)}
          />
        </Field>
      </div>
      {source === 'sqmeter' ? <SqmeterLocation {...props} /> : <TypedLocation {...props} />}
    </>
  );
};

// The location as the settings have it, when there is no live status.
const keptStatus = (location: LocationConfig): LocationStatus => ({
  source: 'sqmeter',
  set: location.set,
  gps: location.sqmeter?.gps ?? false,
  savedAt: location.sqmeter?.savedAt || null,
  readAgeSeconds: null,
  problem: null,
});

// The location the device keeps from SQMeter: never edited here.
const SqmeterLocation: FunctionalComponent<LocationProps> = ({ location, status, sqmeterUrl, onSetAddress }) => {
  if (sqmeterUrl.trim() === '')
    return (
      <Note tone="warn" action={onSetAddress && { label: t('settings.roof.setSqmeterAddress'), onClick: onSetAddress }}>
        {t('settings.roof.locationNoAddress')}
      </Note>
    );
  const live = status?.location;
  // Just switched to SQMeter and not saved yet: the device reads it once saved.
  if (live && live.source !== 'sqmeter') return <Note>{t('settings.roof.locationReadOnceSaved')}</Note>;
  const kept = live ?? keptStatus(location);
  const line = locationLine(kept);
  const set = kept.set;
  return (
    <div data-inventory="settings-location-sqmeter">
      <ReadingRow
        label={t('settings.roof.location')}
        value={set ? formatCoordinates(location.latitude, location.longitude, 4) : t('settings.roof.locationNotRead')}
      />
      {line?.why && <Note tone="warn">{line.why}</Note>}
      {line && <Note tone={line.problem && !line.why ? 'warn' : 'muted'}>{line.text}</Note>}
    </div>
  );
};

const TypedLocation: FunctionalComponent<LocationProps> = ({ location, update, error }) => (
  <>
    <Toggle
      dataField="location.set"
      label={t('settings.roof.locationSet')}
      hint={t('settings.roof.locationSetHint')}
      checked={location.set}
      onChange={(v) => update(['location', 'set'], v)}
    />
    {(location.set || error('location.latitude') !== undefined || error('location.longitude') !== undefined) && (
      <div class="form-grid indent">
        <DegreesField
          at={['location', 'latitude']}
          label={t('settings.roof.latitude')}
          hint={t('settings.roof.latitudeHint')}
          value={location.latitude}
          update={update}
          error={error}
        />
        <DegreesField
          at={['location', 'longitude']}
          label={t('settings.roof.longitude')}
          hint={t('settings.roof.longitudeHint')}
          value={location.longitude}
          update={update}
          error={error}
        />
      </div>
    )}
  </>
);
