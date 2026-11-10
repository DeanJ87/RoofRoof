import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { AlertEventKey } from '../../types';
import { minutesToSeconds, secondsToMinutes } from './alertSchedule';
import { LEVEL_HINT, type AlertsView } from './alertsShared';
import { AlertEventRow } from './AlertEventRow';
import { Field, NumberInput, SettingsCard } from './controls';
import { t } from '../../i18n';

const EventRows: FunctionalComponent<{ view: AlertsView }> = ({ view }) => {
  const [editing, setEditing] = useState<AlertEventKey | null>(null);
  const row = (key: AlertEventKey, label: string, hint?: string) => (
    <AlertEventRow
      view={view}
      eventKey={key}
      label={label}
      hint={hint}
      editing={editing === key}
      onEdit={() => setEditing(editing === key ? null : key)}
    />
  );
  return (
    <>
      {row('client_lost', t('settings.alerts.theImagingAppStopsChecking'), t('settings.alerts.noRequestReachedTheDevice'))}
      {row('client_back', t('settings.alerts.theImagingAppIsBack'), t('settings.alerts.itStartedCheckingAgainAfter'))}
      {row('client_disconnected', t('settings.alerts.theImagingAppDisconnects'), t('settings.alerts.itDisconnectedNormallyForExample'))}
    </>
  );
};

// "Notify me when": one row per event, then the timing rules. Spec 007 adds
// the roof's events.
export const AlertsEventsCard: FunctionalComponent<{ view: AlertsView }> = ({ view }) => {
  const { alerts, off, set, err, pushoverOn } = view;
  return (
    <SettingsCard title={t('settings.alerts.notifyMeWhen')} hint={LEVEL_HINT}>
      <fieldset class="card-body" disabled={off}>
        <div class={`event-table${pushoverOn ? ' with-sound' : ''}`}>
          <div class="event-row event-table-head" aria-hidden="true">
            <span />
            <span>{t('settings.alerts.level')}</span>
            {pushoverOn && <span>{t('settings.alerts.pushoverSound')}</span>}
            <span />
            <span />
          </div>
          <EventRows view={view} />
        </div>
        <div class="form-grid">
          <Field label={t('settings.alerts.silentFor')} error={err('clientSilentSeconds')} hint={t('settings.alerts.howLongWithoutARequest')}>
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
