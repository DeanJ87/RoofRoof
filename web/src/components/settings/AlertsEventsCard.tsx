import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { AlertEventKey } from '../../types';
import { minutesToSeconds, secondsToMinutes } from './alertSchedule';
import { LEVEL_HINT, type AlertsView } from './alertsShared';
import { AlertEventRow } from './AlertEventRow';
import { Field, Group, NumberInput, SettingsCard } from './controls';
import { Note } from '../ui';
import { t } from '../../i18n';

type RowSpec = [AlertEventKey, string, string];

// The roof's critical events (spec 007): sent whatever the master switch says.
const CRITICAL_ROWS: RowSpec[] = [
  ['mount_not_parked', t('settings.alerts.theMountDoesntPark'), t('settings.alerts.theMountDoesntParkHint')],
  ['roof_not_closed', t('settings.alerts.theRoofDoesntClose'), t('settings.alerts.theRoofDoesntCloseHint')],
];
const ROOF_ROWS: RowSpec[] = [
  ['move_fault', t('settings.alerts.theRoofFaults'), t('settings.alerts.theRoofFaultsHint')],
  ['supply_low', t('settings.alerts.theSupplyIsLow'), t('settings.alerts.theSupplyIsLowHint')],
  ['unsafe_closing', t('settings.alerts.theWeatherClosesTheRoof'), t('settings.alerts.theWeatherClosesTheRoofHint')],
  ['roof_closed', t('settings.alerts.theRoofClosedAfterUnsafe'), t('settings.alerts.theRoofClosedAfterUnsafeHint')],
  ['safety_lost', t('settings.alerts.aWeatherSourceIsLost'), t('settings.alerts.aWeatherSourceIsLostHint')],
  ['daylight_open', t('settings.alerts.theRoofIsOpenInDaylight'), t('settings.alerts.theRoofIsOpenInDaylightHint')],
  ['daylight_closed', t('settings.alerts.theRoofClosedAfterDaylight'), t('settings.alerts.theRoofClosedAfterDaylightHint')],
  ['daylight_unknown', t('settings.alerts.theDaylightCheckStopsWorking'), t('settings.alerts.theDaylightCheckStopsWorkingHint')],
];
const CLIENT_ROWS: RowSpec[] = [
  ['client_lost', t('settings.alerts.theImagingAppStopsChecking'), t('settings.alerts.noRequestReachedTheDevice')],
  ['client_back', t('settings.alerts.theImagingAppIsBack'), t('settings.alerts.itStartedCheckingAgainAfter')],
  ['client_disconnected', t('settings.alerts.theImagingAppDisconnects'), t('settings.alerts.itDisconnectedNormallyForExample')],
];

interface Editing {
  editing: AlertEventKey | null;
  setEditing: (key: AlertEventKey | null) => void;
}

const EventTable: FunctionalComponent<{ view: AlertsView; rows: RowSpec[]; edit: Editing }> = ({ view, rows, edit }) => (
  <div class={`event-table${view.pushoverOn ? ' with-sound' : ''}`}>
    <div class="event-row event-table-head" aria-hidden="true">
      <span />
      <span>{t('settings.alerts.level')}</span>
      {view.pushoverOn && <span>{t('settings.alerts.pushoverSound')}</span>}
      <span />
      <span />
    </div>
    {rows.map(([key, label, hint]) => (
      <AlertEventRow
        key={key}
        view={view}
        eventKey={key}
        label={label}
        hint={hint}
        editing={edit.editing === key}
        onEdit={() => edit.setEditing(edit.editing === key ? null : key)}
      />
    ))}
  </div>
);

// "Notify me when": the roof's critical events (outside the master switch),
// then the roof's other events and the imaging app's, then the timing rules.
export const AlertsEventsCard: FunctionalComponent<{ view: AlertsView }> = ({ view }) => {
  const { alerts, off, set, err } = view;
  const [editing, setEditing] = useState<AlertEventKey | null>(null);
  const edit = { editing, setEditing };
  return (
    <SettingsCard title={t('settings.alerts.notifyMeWhen')} hint={LEVEL_HINT}>
      <Group title={t('settings.alerts.roofSafety')}>
        <EventTable view={view} rows={CRITICAL_ROWS} edit={edit} />
        <Note>{t('settings.alerts.roofSafetyNote')}</Note>
      </Group>
      <fieldset class="card-body" disabled={off}>
        <Group title={t('settings.alerts.roof')}>
          <EventTable view={view} rows={ROOF_ROWS} edit={edit} />
        </Group>
        <Group title={t('settings.alerts.imagingApp')}>
          <EventTable view={view} rows={CLIENT_ROWS} edit={edit} />
        </Group>
        <div class="form-grid">
          <Field
            label={t('settings.alerts.silentFor')}
            error={err('clientSilentSeconds')}
            hint={t('settings.alerts.howLongWithoutARequest')}
          >
            <NumberInput
              min={0.5}
              max={60}
              step={0.5}
              unit="min"
              ariaLabel={t('settings.alerts.silentFor')}
              value={secondsToMinutes(alerts.clientSilentSeconds)}
              disabled={off}
              onChange={(v) => set(['clientSilentSeconds'], minutesToSeconds(v))}
            />
          </Field>
          <Field label={t('settings.alerts.cooldown')} error={err('cooldownSeconds')} hint={t('settings.alerts.minimumGapBetweenAlertsOf')}>
            <NumberInput
              integer
              min={0}
              max={86400}
              unit="s"
              value={alerts.cooldownSeconds}
              disabled={off}
              onChange={(v) => set(['cooldownSeconds'], v)}
            />
          </Field>
        </div>
      </fieldset>
    </SettingsCard>
  );
};
