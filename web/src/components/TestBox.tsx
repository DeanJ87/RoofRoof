import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useEffect, useRef, useState } from 'preact/hooks';
import type { Config, RoofTestBox, TestBoxConfig } from '../types';
import { useConfigEditor, type SaveOutcome } from '../hooks/useConfigEditor';
import { TEST_BOX_CHANGED, saveConfig } from '../lib/settingsApi';
import { effectiveEntries, fixTarget, viewOf, type DepEntry } from '../lib/settingsDeps';
import { getConfigValidationMessage } from '../validation/configSchema';
import { deviceError } from '../i18n/deviceMessage';
import { DepNote, Field, NumberInput, StatusBadge, Toggle } from './settings/controls';
import { listReasons } from './settings/restart';
import { Button, Card, Note, Pill, ReadingRow } from './ui';
import { showToast } from './toast';
import { MODE_PILL, formatUntil, modeOf, type TestBoxMode } from './TestBoxBanner';
import { RequestParkToggle } from './settings/TestBoxCard';
import { parkBypassChanges, parkSource } from '../lib/parkSource';
import { t } from '../i18n';

// The test box page (spec 022, its home since the banner fix): one place to
// see the mode (on until when, which checks are bypassed), turn it off, extend
// it, and choose the bypasses. Reached from the banner, Settings → Roof and the
// setup wizard. The device stamps the deadline and refuses a bypass beside the
// source it stands in for; this page shows what it says.

const goToSettings = (tab: string, anchor?: string) => route(`/settings?tab=${tab}${anchor ? `&section=${anchor}` : ''}`);

// Sends one change straight to the device (Turn off, Extend). A clean page reads
// the settings again on TEST_BOX_CHANGED; one with edits reads them here (Turn off drops them).
const useDirectSave = (reload: () => void, loadStatus: () => void, dirty: boolean) => {
  const [busy, setBusy] = useState<'' | 'off' | 'extend'>('');
  const send = async (which: 'off' | 'extend', testBox: Partial<TestBoxConfig>, done: string) => {
    setBusy(which);
    let ok = false;
    try {
      const answer = await saveConfig({ testBox });
      ok = answer.ok && answer.body?.success !== false;
      if (ok) showToast({ message: done });
      else showToast({ message: deviceError(answer.body, t('settings.failedToSaveSettings')), tone: 'bad' });
    } catch {
      showToast({ message: t('settings.couldNotReachTheDevice'), tone: 'bad' });
    } finally {
      setBusy('');
      if (dirty) reload();
      loadStatus();
      window.dispatchEvent(new Event(TEST_BOX_CHANGED));
    }
    return ok;
  };
  return { busy, send };
};

const CHECKS = ['park', 'weather', 'daylight'] as const;

// Each bypassable check: Bypassed (in effect now) or Checked.
const CheckRows: FunctionalComponent<{ live: RoofTestBox }> = ({ live }) => (
  <>
    {live.until > 0 && <ReadingRow label={t('testBox.mode.ends')} value={formatUntil(live.until)} />}
    {CHECKS.map((check) => (
      <div class="reading-row" key={check} data-test-box-check={check}>
        <span class="reading-label">{t(`testBox.check.${check}`)}</span>
        {live[check] ? (
          <StatusBadge tone="warn" label={t('testBox.check.bypassed')} />
        ) : (
          <StatusBadge tone="ok" label={t('testBox.check.checked')} />
        )}
      </div>
    ))}
  </>
);

interface ModeProps {
  live: RoofTestBox | undefined;
  savedSet: boolean; // any bypass in the saved settings
  hours: number;
  busy: '' | 'off' | 'extend';
  dirty: boolean;
  saving: boolean;
  onTurnOff: () => void;
  onExtend: () => void;
}

// Turn off (the step not to forget), then Extend: not while this page has unsaved edits.
const ModeActions: FunctionalComponent<ModeProps> = ({ busy, dirty, saving, hours, onTurnOff, onExtend, live }) => (
  <>
    <Note>{t('testBox.mode.turnOffFirst')}</Note>
    <Button variant="danger" block onClick={onTurnOff} busy={busy === 'off'} disabled={busy === 'extend'}>
      {t('testBox.mode.turnOff')}
    </Button>
    {live && (
      <div class="btn-row">
        <Button
          onClick={onExtend}
          busy={busy === 'extend'}
          disabled={busy !== '' || saving || dirty}
          describedBy={dirty ? 'test-box-extend-why' : undefined}
        >
          {t('testBox.mode.extend', { hours })}
        </Button>
      </div>
    )}
    {live && dirty && <Note id="test-box-extend-why">{t('testBox.mode.extendDirty')}</Note>}
  </>
);

const modeBody = (mode: TestBoxMode, props: ModeProps) => {
  if (mode === 'off') return <Note>{t('testBox.mode.offNote')}</Note>;
  if (mode === 'unknown')
    return (
      <>
        <Note tone="warn">{t('testBox.mode.unknownNote')}</Note>
        {props.savedSet && <ModeActions {...props} />}
      </>
    );
  return (
    <>
      {props.live && <CheckRows live={props.live} />}
      <ModeActions {...props} />
    </>
  );
};

const ModeCard: FunctionalComponent<ModeProps> = (props) => {
  const mode = modeOf(props.live);
  return (
    <Card
      title={t('testBox.mode.title')}
      icon="warning"
      tone={mode === 'on' ? 'red' : 'amber'}
      actions={<Pill tone={MODE_PILL[mode]}>{t(`testBox.mode.${mode}`)}</Pill>}
    >
      <div class="card-body" data-test-box-mode={mode}>
        {modeBody(mode, props)}
      </div>
    </Card>
  );
};

interface BypassesProps {
  config: Config;
  testBox: TestBoxConfig;
  on: boolean; // any bypass saved
  dirty: boolean;
  saving: boolean;
  busy: boolean; // Turn off or Extend on its way
  error: (key: string) => string | undefined;
  sourceTurnedOff: boolean; // spec 026: this edit turns the saved SQMeter weather source off
  savedSources: { http: boolean; mqtt: boolean }; // the SQMeter sources as saved
  savedNinaUrl: string; // the N.I.N.A. address as saved
  weatherUnsafe: boolean; // a configured source reports unsafe now: a bypass never hides a reading
  update: (path: string[], value: unknown) => void;
  updateMany: (changes: [string[], unknown][]) => void;
  depEntry: DepEntry | undefined;
  onSave: () => void;
  onDiscard: () => void;
}

// Save (Turn on test box when nothing was on), with the warning while turning it on.
const SaveRow: FunctionalComponent<BypassesProps & { turningOn: boolean }> = (props) => {
  if (!props.dirty) return null;
  const startsIt = !props.on && props.turningOn;
  return (
    <>
      {startsIt && <Note tone="bad">{t('testBox.warning')}</Note>}
      <div class="btn-row">
        <Button variant="primary" onClick={props.onSave} busy={props.saving} disabled={props.busy}>
          {startsIt ? t('testBox.turnOn') : t('settings.save')}
        </Button>
        <Button variant="ghost" onClick={props.onDiscard} disabled={props.saving}>
          {t('settings.discard')}
        </Button>
      </div>
    </>
  );
};

const BypassesCard: FunctionalComponent<BypassesProps> = (props) => {
  const { config, testBox, update } = props;
  // Spec 024: with park switches only there is no N.I.N.A. to stand in for.
  const switchesOnly = config.park ? parkSource(config.park) === 'switches' : false;
  const sourceOn = sqmeterOn(config);
  const turningOn = testBox.park || testBox.weather || testBox.daylight;
  const fix = (entry: DepEntry) => {
    const target = fixTarget(entry);
    if ('tab' in target) goToSettings(target.tab, target.anchor);
  };
  return (
    <Card title={t('testBox.bypasses')} icon="off" hint={t('testBox.hint')}>
      <div class="card-body">
        <Toggle
          dataField="testBox.park"
          label={t('testBox.park')}
          hint={t('testBox.parkHint')}
          checked={testBox.park}
          onChange={(v) => props.updateMany(parkBypassChanges(v, config.park?.nina.url ?? '', props.savedNinaUrl))}
          blockedReason={switchesOnly ? t('testBox.parkSwitchesOnly') : undefined}
          onFix={() => goToSettings('roof', 'park')}
          fixLabel={t('testBox.goToPark')}
        />
        {/* Owner decision 2026-11-24: the same save clears the N.I.N.A. address, so a bench box never parks a real mount. */}
        {testBox.park && props.savedNinaUrl.trim() !== '' && <Note tone="warn">{t('testBox.ninaCleared')}</Note>}
        {/* Spec 026: on beside an SQMeter source, the same save turns the source off, so the weather
            check never reads "no weather source" (unsafe) between two saves and starts a close. */}
        <Toggle
          dataField="testBox.weather"
          label={t('testBox.weather')}
          hint={t('testBox.weatherHint')}
          checked={testBox.weather}
          onChange={(v) => props.updateMany(weatherBypassChanges(v, sourceOn, props.savedSources))}
          blockedReason={sourceOn && props.weatherUnsafe ? t('testBox.weatherUnsafe') : undefined}
          onFix={() => goToSettings('roof', 'safety')}
          fixLabel={t('move.fix.weather')}
        />
        {testBox.weather && props.sourceTurnedOff && <Note tone="warn">{t('testBox.weatherSourceOff')}</Note>}
        <Toggle
          dataField="testBox.daylight"
          label={t('testBox.daylight')}
          hint={t('testBox.daylightHint')}
          checked={testBox.daylight}
          onChange={(v) => update(['testBox', 'daylight'], v)}
        />
        {props.depEntry && <DepNote entry={props.depEntry} onFix={fix} />}
        <div class="form-grid">
          <Field label={t('testBox.hours')} hint={t('testBox.hoursHint')} error={props.error('testBox.hours')}>
            <NumberInput
              dataField="testBox.hours"
              value={testBox.hours}
              min={1}
              max={720}
              unit="h"
              integer
              onChange={(v) => update(['testBox', 'hours'], v)}
            />
          </Field>
        </div>
        {config.park && <RequestParkToggle park={config.park} update={update} />}
        <SaveRow {...props} turningOn={turningOn} />
      </div>
    </Card>
  );
};

const useSave = (editor: ReturnType<typeof useConfigEditor>) => {
  const [saving, setSaving] = useState(false);
  const report = (outcome: SaveOutcome) => {
    if (outcome.kind === 'saved') showToast({ message: t('settings.saved') });
    else if (outcome.kind === 'invalid') showToast({ message: getConfigValidationMessage(outcome.errors), tone: 'bad' });
    else if (outcome.kind === 'refused') showToast({ message: outcome.message, tone: 'bad' });
    else if (outcome.kind === 'conflict')
      showToast({
        message: t('settings.changedOnDevice', { fields: listReasons(outcome.fields) }),
        tone: 'warn',
        durationMs: 0,
        action: { label: t('settings.reload'), onClick: () => void editor.rebase(outcome.fields) },
      });
    else showToast({ message: t('settings.couldNotReachTheDevice'), tone: 'bad' });
  };
  const save = async () => {
    setSaving(true);
    const outcome = await editor.save();
    setSaving(false);
    report(outcome);
  };
  return { saving, save };
};

/**
 * Spec 026: the weather bypass on beside an SQMeter source turns the source off in the same save (so the
 * weather never reads "no weather source" between two saves); off again, the saved sources come back.
 */
const weatherBypassChanges = (on: boolean, sourceOn: boolean, saved: { http: boolean; mqtt: boolean }): [string[], unknown][] => {
  if (on && sourceOn)
    return [
      [['testBox', 'weather'], true],
      [['safety', 'http', 'enabled'], false],
      [['safety', 'mqtt', 'enabled'], false],
    ];
  if (on) return [[['testBox', 'weather'], true]];
  return [
    [['testBox', 'weather'], false],
    [['safety', 'http', 'enabled'], saved.http],
    [['safety', 'mqtt', 'enabled'], saved.mqtt],
  ];
};

// The weather sources as the bypass sees them: turned off by this edit, as saved, and reading unsafe now.
const weatherSource = (saved: Config, edited: Config, weather: string | undefined) => ({
  sourceTurnedOff: sqmeterOn(saved) && !sqmeterOn(edited),
  savedSources: { http: Boolean(saved.safety?.http.enabled), mqtt: Boolean(saved.safety?.mqtt.enabled) },
  weatherUnsafe: weather === 'unsafe',
});

// The N.I.N.A. address as saved ('' when none).
const savedNina = (saved: Config) => saved.park?.nina.url ?? '';

// An SQMeter weather source is on in these settings.
const sqmeterOn = (c: Config) => Boolean(c.safety?.http.enabled || c.safety?.mqtt.enabled);

const TestBox: FunctionalComponent = () => {
  const editor = useConfigEditor();
  const { config, saved, status, effective, dirty, loading } = editor;
  const { saving, save } = useSave(editor);
  const direct = useDirectSave(editor.reload, editor.loadStatus, dirty);
  const heading = useRef<HTMLHeadingElement>(null);
  // Saved elsewhere (another page, the wizard, the API, Turn off): read the settings again unless edited here.
  const reload = useRef(editor.reload);
  reload.current = editor.reload;
  useEffect(() => {
    if (dirty) return undefined;
    const read = () => reload.current();
    window.addEventListener(TEST_BOX_CHANGED, read);
    return () => window.removeEventListener(TEST_BOX_CHANGED, read);
  }, [dirty]);
  if (loading) return <div class="empty-state">{t('settings.loadingSettings')}</div>;
  if (!config?.testBox || !saved?.testBox) return <div class="empty-state tone-red">{t('settings.failedToLoadConfiguration')}</div>;
  const savedBox = saved.testBox;
  const deps = viewOf(effectiveEntries(config, effective, dirty));
  const turnOff = async () => {
    const done = dirty ? t('testBox.turnedOffDropped') : t('testBox.turnedOff');
    // The button goes once it's off: focus moves to the page's title.
    if (await direct.send('off', { park: false, weather: false, daylight: false }, done)) heading.current?.focus();
  };
  return (
    <div class="panel-page compact-page page-enter" data-test-box-page>
      <div>
        <h2 class="page-title" tabIndex={-1} ref={heading}>
          {t('testBox.title')}
        </h2>
        <p class="subtext">{t('testBox.intro')}</p>
      </div>
      <ModeCard
        live={status?.roof?.testBox}
        savedSet={savedBox.park || savedBox.weather || savedBox.daylight}
        hours={savedBox.hours}
        busy={direct.busy}
        dirty={dirty}
        saving={saving}
        onTurnOff={() => void turnOff()}
        onExtend={() => void direct.send('extend', { hours: savedBox.hours }, t('testBox.renewed', { hours: savedBox.hours }))}
      />
      <BypassesCard
        config={config}
        testBox={config.testBox}
        on={savedBox.park || savedBox.weather || savedBox.daylight}
        dirty={dirty}
        saving={saving}
        busy={direct.busy !== ''}
        error={(key) => editor.validationErrors[key]}
        update={(path, value) => editor.applyChanges([[path, value]])}
        updateMany={(changes) => editor.applyChanges(changes)}
        {...weatherSource(saved, config, status?.roof?.weather)}
        savedNinaUrl={savedNina(saved)}
        depEntry={deps.get('testBox.park')}
        onSave={() => void save()}
        onDiscard={editor.discard}
      />
    </div>
  );
};

export default TestBox;
