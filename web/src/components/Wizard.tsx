import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useEffect, useRef, useState } from 'preact/hooks';
import type { Config, SetupConfig, SystemStatus } from '../types';
import { useConfigEditor, type SaveOutcome } from '../hooks/useConfigEditor';
import { useWebSocket } from '../hooks/useWebSocket';
import type { TestSave } from '../hooks/useInputsTest';
import { getConfigValidationMessage } from '../validation/configSchema';
import { fieldErrorAliases } from './settings/payload';
import { listReasons } from './settings/restart';
import { MotionNote } from './settings/RoofTab';
import { effectiveEntries, fixTarget, viewOf } from '../lib/settingsDeps';
import { restartDevice } from '../lib/settingsApi';
import { saveSetup } from '../lib/setupApi';
import { locationQuery } from './settings/tabs';
import type { SettingsTabProps } from './settings/context';
import type { WizardPageProps } from './wizard/context';
import { WIZARD_PAGES, clampStep, type WizardPageId } from './wizard/pages';
import { DevicePage, SitePage } from './wizard/DevicePages';
import { HallPage, HardwarePage, MotionPage } from './wizard/HardwarePages';
import { AlertsPage, DaylightPage, NinaPage, WeatherPage } from './wizard/SourcePages';
import { ReviewPage } from './wizard/ReviewPage';
import { showToast } from './toast';
import { Button, Note } from './ui';
import { t } from '../i18n';

// The setup wizard (spec 015): ten pages of the essential settings, each
// saved through the settings API like Settings (the device refuses a save
// while the roof moves), and a review. It offers no roof command: the motion
// tests belong to the attended commissioning (constitution I, III). It is a
// page of the app, so the Stop bar is on it while the roof moves.

const PAGES: Record<WizardPageId, FunctionalComponent<WizardPageProps>> = {
  device: DevicePage,
  site: SitePage,
  hardware: HardwarePage,
  hall: HallPage,
  motion: MotionPage,
  weather: WeatherPage,
  nina: NinaPage,
  alerts: AlertsPage,
  daylight: DaylightPage,
  review: ReviewPage,
};

const LAST = WIZARD_PAGES.length - 1;

// ?step=1 opens page 1 (System's "Run setup wizard"); otherwise the saved page while setup isn't done.
const startStep = (setup: SetupConfig | undefined): number => {
  const query = new URLSearchParams(typeof window !== 'undefined' ? locationQuery(window.location) : '');
  if (query.has('step')) return clampStep(Number(query.get('step')) - 1);
  return setup && setup.state !== 'done' ? clampStep(setup.step) : 0;
};

const valueAt = (config: Config | null, path: string): unknown =>
  path
    .split('.')
    .reduce<unknown>((node, key) => (node && typeof node === 'object' ? (node as Record<string, unknown>)[key] : undefined), config);

const outcomeText = (outcome: SaveOutcome): string | null => {
  if (outcome.kind === 'invalid') return getConfigValidationMessage(outcome.errors);
  if (outcome.kind === 'refused') return outcome.message;
  if (outcome.kind === 'unreachable') return t('settings.couldNotReachTheDevice');
  return null;
};

const focusField = (errors: Record<string, string>) => {
  const first = Object.keys(errors)[0];
  if (!first) return;
  const alias = Object.entries(fieldErrorAliases).find(([, path]) => path === first)?.[0];
  setTimeout(() => {
    const target =
      document.querySelector<HTMLElement>(`[data-field="${first}"]`) ??
      (alias ? document.querySelector<HTMLElement>(`[data-field="${alias}"]`) : null);
    target?.focus?.();
  }, 60);
};

const PageList: FunctionalComponent<{ step: number; go: (index: number) => void }> = ({ step, go }) => (
  <nav class="settings-tabs" aria-label={t('wizard.pagesLabel')}>
    {WIZARD_PAGES.map((page, index) => (
      <button
        key={page.id}
        type="button"
        class={`settings-tab ${index === step ? 'is-active' : ''}`}
        aria-current={index === step ? 'step' : undefined}
        onClick={() => go(index)}
      >
        {t('wizard.pageNumber', { number: index + 1, title: page.title })}
      </button>
    ))}
  </nav>
);

const SkipSetup: FunctionalComponent<{ onConfirm: () => void; onCancel: () => void; busy: boolean }> = ({ onConfirm, onCancel, busy }) => (
  <div class="card-group" data-skip-warning>
    <Note tone="bad">{t('wizard.skipWarning')}</Note>
    <div class="btn-row">
      <Button variant="danger" onClick={onConfirm} busy={busy}>
        {t('wizard.skipConfirm')}
      </Button>
      <Button variant="ghost" onClick={onCancel}>
        {t('wizard.keepGoing')}
      </Button>
    </div>
  </div>
);

const Finished: FunctionalComponent<{ restart: string[] }> = ({ restart }) => (
  <div class="card-group" data-finished>
    <Note tone="ok">{t('wizard.finished')}</Note>
    {restart.length > 0 && (
      <Note tone="warn" action={{ label: t('settings.restart'), onClick: () => void restartDevice().catch(() => undefined) }}>
        {t('wizard.restartToApply', { listReasons: listReasons(restart) })}
      </Note>
    )}
    <div class="btn-row">
      <Button variant="primary" onClick={() => route('/')}>
        {t('wizard.toDashboard')}
      </Button>
    </div>
  </div>
);

interface ActionsProps {
  step: number;
  dirty: boolean;
  busy: boolean;
  skipping: boolean;
  go: (index: number) => void;
  saveHere: () => void;
  next: () => void;
  finish: () => void;
  askSkip: () => void;
}

const Actions: FunctionalComponent<ActionsProps> = ({ step, dirty, busy, skipping, go, saveHere, next, finish, askSkip }) => (
  <div class="btn-row" data-wizard-actions>
    {step > 0 && (
      <Button variant="ghost" onClick={() => go(step - 1)}>
        {t('wizard.back')}
      </Button>
    )}
    {step < LAST && (
      <Button variant="ghost" onClick={() => go(step + 1)}>
        {t('wizard.skipPage')}
      </Button>
    )}
    {step < LAST && dirty && (
      <Button onClick={saveHere} busy={busy} busyLabel={t('common.saving')}>
        {t('settings.save')}
      </Button>
    )}
    <Button variant="primary" onClick={step < LAST ? next : finish} busy={busy} busyLabel={t('common.saving')}>
      {step < LAST ? t('wizard.saveAndContinue') : t('wizard.finish')}
    </Button>
    {!skipping && (
      <Button variant="link" onClick={askSkip}>
        {t('wizard.skipSetup')}
      </Button>
    )}
  </div>
);

// Where the wizard starts, once the settings are in.
const useStartStep = (loading: boolean, setup: SetupConfig | undefined) => {
  const [step, setStep] = useState<number | null>(null);
  useEffect(() => {
    if (!loading) setStep((current) => current ?? startStep(setup));
  }, [loading, setup]);
  return [step, setStep] as const;
};

// Each page change moves focus to the page's heading (FR-019), not the first render.
const useHeadingFocus = (step: number | null) => {
  const heading = useRef<HTMLHeadingElement>(null);
  const moved = useRef(false);
  useEffect(() => {
    if (step === null) return;
    if (moved.current) heading.current?.focus();
    moved.current = true;
  }, [step]);
  return heading;
};

const Wizard: FunctionalComponent = () => {
  const editor = useConfigEditor();
  const { data: live } = useWebSocket<SystemStatus>('/ws/status');
  const { config, saved, setup, effective, loading, validationErrors, dirty } = editor;
  const [step, setStep] = useStartStep(loading, setup);
  const [hallNotFitted, setHallNotFitted] = useState(false);
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const [skipping, setSkipping] = useState(false);
  const [finished, setFinished] = useState(false);
  const [restart, setRestart] = useState<string[]>([]);
  const heading = useHeadingFocus(step);

  if (loading || step === null) {
    return (
      <div class="empty-state">
        <h2>{t('settings.loadingSettings')}</h2>
      </div>
    );
  }
  if (!config || !saved) return <div class="empty-state tone-red">{t('settings.failedToLoadConfiguration')}</div>;

  const state = setup?.state ?? 'new';
  const status = live ?? editor.status;
  const go = (index: number) => {
    setProblem(null);
    setSkipping(false);
    setStep(index);
    window.scrollTo?.({ top: 0 });
  };

  // One save of the page; false (with the reason shown) when it didn't go through.
  const save = async (next: SetupConfig | undefined): Promise<boolean> => {
    setBusy(true);
    const outcome = await editor.save(next ? { setup: next } : {});
    setBusy(false);
    const text = outcomeText(outcome);
    setProblem(text);
    if (outcome.kind === 'invalid') focusField(outcome.errors);
    if (outcome.kind === 'saved' && outcome.restart.length > 0) setRestart((before) => [...new Set([...before, ...outcome.restart])]);
    return outcome.kind === 'saved';
  };

  const saveForTest =
    (fields: string[]): TestSave =>
    async () => {
      if (!dirty) return { ok: true, changed: false, config };
      const changed = fields.some((field) => valueAt(saved, field) !== valueAt(config, field));
      return (await save(undefined)) ? { ok: true, changed, config } : { ok: false, message: t('wizard.test.notSaved') };
    };

  const next = async () => {
    if (await save({ state, step: step + 1 })) go(step + 1);
  };
  const finish = async () => {
    if (await save({ state: 'done', step: 0 })) setFinished(true);
  };
  const skip = async () => {
    setBusy(true);
    const ok = await saveSetup({ state: 'done', step: 0 });
    setBusy(false);
    if (!ok) return setProblem(t('wizard.skipFailed'));
    showToast({ message: t('wizard.skipped') });
    route('/');
  };

  const props: WizardPageProps = {
    config,
    update: (path, value) => editor.applyChanges([[path, value]]),
    updateMany: editor.applyChanges,
    error: (key) => validationErrors[fieldErrorAliases[key] ?? key],
    status,
    dirty,
    goTo: (tab, anchor) => route(`/settings?tab=${tab}${anchor ? `&section=${anchor}` : ''}`),
    deps: viewOf(effectiveEntries(config, effective, dirty)),
    fix: (entry) => {
      const target = fixTarget(entry);
      if (!('restart' in target)) route(`/settings?tab=${target.tab}`);
    },
    hallNotFitted,
    setHallNotFitted,
    saveForTest,
    goToPage: go,
  } satisfies SettingsTabProps & WizardPageProps;
  const Page = PAGES[WIZARD_PAGES[step].id];

  return (
    <div class="panel-page settings-page page-enter" data-wizard-page={WIZARD_PAGES[step].id}>
      <PageList step={step} go={go} />
      <h2 class="wizard-heading" ref={heading} tabIndex={-1}>
        {t('wizard.heading', { number: step + 1, total: WIZARD_PAGES.length, title: WIZARD_PAGES[step].title })}
      </h2>
      <MotionNote status={status} />
      <div class="settings-tab-panel">
        <Page {...props} />
      </div>
      {problem && (
        <Note tone="bad" id="wizard-problem">
          {problem}
        </Note>
      )}
      {skipping && <SkipSetup onConfirm={() => void skip()} onCancel={() => setSkipping(false)} busy={busy} />}
      {finished ? (
        <Finished restart={restart} />
      ) : (
        <Actions
          step={step}
          dirty={dirty}
          busy={busy}
          skipping={skipping}
          go={go}
          saveHere={() => void save({ state, step })}
          next={() => void next()}
          finish={() => void finish()}
          askSkip={() => setSkipping(true)}
        />
      )}
    </div>
  );
};

export default Wizard;
