import { FunctionalComponent } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { getConfigValidationMessage, type ValidationErrors } from '../validation/configSchema';
import AlertsTab from './settings/AlertsTab';
import type { SettingsTabProps } from './settings/context';
import DeviceTab from './settings/DeviceTab';
import NetworkTab from './settings/NetworkTab';
import RoofTab from './settings/RoofTab';
import { fieldErrorAliases } from './settings/payload';
import { SETTINGS_TABS, locationQuery, tabForErrorPath, tabFromLocation, type SettingsTabId } from './settings/tabs';
import TimeTab from './settings/TimeTab';
import { listReasons } from './settings/restart';
import { DEP_LABELS } from './settings/depLabels';
import { showToast } from './toast';
import { Button, Note } from './ui';
import { nextTabIndex, scrollBehavior } from '../lib/a11y';
import { effectiveEntries, fixTarget, previewInactive, viewOf, type DepEntry } from '../lib/settingsDeps';
import { t } from '../i18n';
import { restartDevice } from '../lib/settingsApi';
import { useTabFromUrl } from '../hooks/useTabFromUrl';
import { useConfigEditor } from '../hooks/useConfigEditor';

// Saving would switch these off in practice (FR-006); harmless defaults aren't news.
const SavePreview: FunctionalComponent<{ entries: DepEntry[] }> = ({ entries }) =>
  entries.length === 0 ? null : (
    <Note tone="warn">
      <span data-preview="inactive">
        {t('settings.savingMakesTheseInactiveJoin', {
          join: entries.map((e) => `${DEP_LABELS[e.setting] ?? e.setting} (${e.text})`).join(', '),
        })}
      </span>
    </Note>
  );

const Settings: FunctionalComponent = () => {
  const initial = tabFromLocation(typeof window !== 'undefined' ? locationQuery(window.location) : '');
  const [tab, setTab] = useState<SettingsTabId>(initial.tab);
  const editor = useConfigEditor();
  const { config, saved, status, effective, loading, validationErrors, originalWifiSsid, dirty, applyChanges } = editor;
  const [saving, setSaving] = useState(false);
  const pendingAnchor = useRef<string | undefined>(initial.anchor);

  useTabFromUrl(tab, setTab, pendingAnchor);

  // Scroll to a requested section once its tab has rendered.
  const loaded = config !== null;
  useEffect(() => {
    if (!loaded || !pendingAnchor.current) return;
    const anchor = pendingAnchor.current;
    pendingAnchor.current = undefined;
    setTimeout(() => document.getElementById(anchor)?.scrollIntoView({ block: 'start' }), 0);
  }, [loaded, tab]);

  const goTo = (next: SettingsTabId, anchor?: string) => {
    pendingAnchor.current = anchor;
    setTab(next);
    const url = new URL(window.location.href);
    if (url.hash.startsWith('#/')) {
      // Hash routing (demo): the query lives in the hash.
      url.hash = `#/settings?tab=${next}`;
    } else {
      url.searchParams.set('tab', next);
      url.searchParams.delete('section');
    }
    window.history.replaceState(window.history.state, '', `${url.pathname}${url.search}${url.hash}`);
    if (!anchor) window.scrollTo?.({ top: 0 });
  };

  const errorsByTab = useMemo(() => {
    const counts: Partial<Record<SettingsTabId, number>> = {};
    Object.keys(validationErrors).forEach((path) => {
      const owner = tabForErrorPath(path);
      counts[owner] = (counts[owner] ?? 0) + 1;
    });
    return counts;
  }, [validationErrors]);

  const focusFirstError = (errors: ValidationErrors) => {
    const firstField = Object.keys(errors)[0];
    if (!firstField) return;
    const owner = tabForErrorPath(firstField);
    if (owner !== tab) goTo(owner);
    setTimeout(() => {
      const alias = Object.entries(fieldErrorAliases).find(([, path]) => path === firstField)?.[0];
      const target =
        document.querySelector<HTMLElement>(`[data-field="${firstField}"]`) ??
        (alias ? document.querySelector<HTMLElement>(`[data-field="${alias}"]`) : null);
      target?.scrollIntoView?.({ behavior: scrollBehavior(), block: 'center' });
      if (target instanceof HTMLInputElement || target instanceof HTMLSelectElement) target.focus({ preventScroll: true });
    }, 60);
  };

  const restart = async () => {
    try {
      await restartDevice();
      showToast({ message: t('common.restarting') });
    } catch {
      showToast({ message: t('settings.couldNotReachTheDevice'), tone: 'bad' });
    }
  };

  const save = async () => {
    setSaving(true);
    const outcome = await editor.save();
    setSaving(false);
    if (outcome.kind === 'invalid') {
      showToast({ message: getConfigValidationMessage(outcome.errors), tone: 'bad' });
      focusFirstError(outcome.errors);
    } else if (outcome.kind === 'refused') {
      showToast({ message: outcome.message, tone: 'bad' });
    } else if (outcome.kind === 'unreachable') {
      showToast({ message: t('settings.couldNotReachTheDevice'), tone: 'bad' });
    } else {
      showToast(
        outcome.restart.length > 0
          ? {
              message: t('settings.savedRestartToApplyListreasons', { listReasons: listReasons(outcome.restart) }),
              tone: 'warn',
              durationMs: 0,
              action: { label: t('settings.restart'), onClick: restart },
            }
          : { message: t('settings.saved') },
      );
    }
  };

  const discard = editor.discard;

  if (loading) {
    return (
      <div class="empty-state">
        <h2>{t('settings.loadingSettings')}</h2>
      </div>
    );
  }

  if (!config || !saved) {
    return <div class="empty-state tone-red">{t('settings.failedToLoadConfiguration')}</div>;
  }

  const fix = (entry: DepEntry) => {
    const target = fixTarget(entry);
    if ('restart' in target) void restart();
    else if (SETTINGS_TABS.some((t) => t.id === target.tab)) goTo(target.tab as SettingsTabId, target.anchor);
  };

  const onTabKey = (event: KeyboardEvent) => {
    const index = SETTINGS_TABS.findIndex(({ id }) => id === tab);
    const next = nextTabIndex(event.key, index, SETTINGS_TABS.length, document.documentElement.dir === 'rtl');
    if (next === null) return;
    event.preventDefault();
    const nextId = SETTINGS_TABS[next].id;
    goTo(nextId);
    document.getElementById(`settings-tab-${nextId}`)?.focus();
  };

  const props: SettingsTabProps = {
    config,
    update: (path, value) => applyChanges([[path, value]]),
    updateMany: applyChanges,
    error: (key) => validationErrors[fieldErrorAliases[key] ?? key],
    status,
    dirty,
    goTo,
    deps: viewOf(effectiveEntries(config, effective, dirty)),
    fix,
  };

  return (
    <div class="panel-page settings-page page-enter">
      {/* ARIA tabs: arrows, Home and End move between tabs (SQMeter spec 022). */}
      <div class="settings-tabs" role="tablist" aria-label={t('settings.settingsSections')} onKeyDown={onTabKey}>
        {SETTINGS_TABS.map(({ id, label }) => (
          <button
            key={id}
            id={`settings-tab-${id}`}
            type="button"
            role="tab"
            aria-selected={tab === id}
            aria-controls="settings-tab-panel"
            aria-describedby={errorsByTab[id] ? `settings-tab-${id}-errors` : undefined}
            tabIndex={tab === id ? 0 : -1}
            class={`settings-tab ${tab === id ? 'is-active' : ''}`}
            onClick={() => goTo(id)}
          >
            {label}
            {errorsByTab[id] ? (
              <span class="settings-tab-error" title={t('settings.errorsToFix', { count: errorsByTab[id] })} aria-hidden="true" />
            ) : null}
          </button>
        ))}
        {/* The red dot in words, as each tab's description. */}
        {SETTINGS_TABS.map(({ id }) =>
          errorsByTab[id] ? (
            <span key={id} id={`settings-tab-${id}-errors`} hidden>
              {t('settings.errorsToFix', { count: errorsByTab[id] })}
            </span>
          ) : null,
        )}
      </div>

      <div class="settings-tab-panel" role="tabpanel" id="settings-tab-panel" aria-labelledby={`settings-tab-${tab}`} tabIndex={0}>
        {tab === 'device' && <DeviceTab {...props} />}
        {tab === 'roof' && <RoofTab {...props} />}
        {tab === 'network' && <NetworkTab {...props} originalWifiSsid={originalWifiSsid} />}
        {tab === 'time' && <TimeTab {...props} />}
        {tab === 'alerts' && <AlertsTab {...props} />}
      </div>

      <SavePreview entries={previewInactive(config, saved, effective, dirty)} />
      {dirty && (
        <div class="save-bar">
          <span class="save-bar-state">{t('settings.unsavedChanges')}</span>
          <Button variant="ghost" onClick={discard}>
            {t('settings.discard')}
          </Button>
          <Button variant="primary" onClick={save} busy={saving} busyLabel={t('common.saving')}>
            {t('settings.save')}
          </Button>
        </div>
      )}
    </div>
  );
};

export default Settings;
