import { FunctionalComponent, type ComponentChildren, type Ref } from 'preact';
import { route } from 'preact-router';
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import type { Config, RoofInputs, SetupConfig, SystemStatus } from '../types';
import { useConfigEditor, type SaveOutcome } from '../hooks/useConfigEditor';
import { useWebSocket } from '../hooks/useWebSocket';
import type { TestSave } from '../hooks/useInputsTest';
import { getConfigValidationMessage } from '../validation/configSchema';
import { fieldErrorAliases } from './settings/payload';
import { MotionNote } from './settings/RoofTab';
import { effectiveEntries, fixTarget, viewOf } from '../lib/settingsDeps';
import { fetchInputs, saveSetup } from '../lib/setupApi';
import { locationQuery } from './settings/tabs';
import type { WizardPageProps } from './wizard/context';
import { clampStep } from './wizard/pages';
import {
  SCREENS,
  firstScreenOf,
  isAnswered,
  nextScreen,
  previousScreen,
  progressOf,
  screenIndex,
  settle,
  stepOf,
  type ScreenId,
} from './wizard/screens';
import { DoneScreen } from './wizard/ReviewPage';
import { Footer, Header, LeaveChoices, RestartNote } from './wizard/Chrome';
import RoofStopBar from './RoofStopBar';
import { markWizardOpened } from './SetupPrompt';
import { showToast } from './toast';
import { Button, Modal, Note, SpeechBubble } from './ui';
import { t } from '../i18n';

// The setup wizard (specs 015 and 018): a dialog over the app that asks one
// question per screen. Each screen saves through the settings API like
// Settings (the device refuses a save while the roof moves). It offers no roof
// command: the motion tests belong to the attended commissioning (constitution
// I, III). The Stop bar is inside the dialog while the roof moves (VII).

const QUESTION_ID = 'wizard-question';

// Where the dialog was opened from, so Leave for now goes back there.
let returnTo = '/';
export const noteRoute = (url: string, previous?: string) => {
  if (url.startsWith('/wizard') && previous && !previous.startsWith('/wizard')) returnTo = previous;
};

// ?screen=<id> opens that screen, ?step=N spec 015's page N (System's "Run
// setup wizard"); otherwise the saved page while setup isn't done.
const startScreen = (setup: SetupConfig | undefined): number => {
  const query = new URLSearchParams(typeof window !== 'undefined' ? locationQuery(window.location) : '');
  const screen = screenIndex(query.get('screen') ?? '');
  if (screen >= 0) return screen;
  if (query.has('step')) return firstScreenOf(clampStep(Number(query.get('step')) - 1));
  return setup && setup.state !== 'done' ? firstScreenOf(clampStep(setup.step)) : 0;
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

type Mode = 'screen' | 'leave' | 'skip' | 'done';

const MISSING: Partial<Record<ScreenId, string>> = { name: t('wizard.missing.name'), password: t('wizard.missing.password') };

// The question and its "why" for the screen or the dialog's own screens.
const textsOf = (mode: Mode, id: ScreenId): [string, string] => {
  const key = mode === 'screen' ? id : mode === 'skip' ? 'leave' : mode;
  return [t(`wizard.q.${key}`), t(`wizard.why.${key}`)];
};

// Focus goes to the question on every new screen (FR-003): the dialog's name.
const useQuestionFocus = (key: string) => {
  const heading = useRef<HTMLHeadingElement>(null);
  // In the commit, so nothing typed or pressed right after a change lands elsewhere.
  useLayoutEffect(() => {
    heading.current?.focus();
  }, [key]);
  return heading;
};

interface View {
  mode: Mode;
  index: number;
  props: WizardPageProps;
  busy: boolean;
  problem: string | null;
  restart: string[];
  heading: Ref<HTMLHeadingElement>;
  go: (index: number) => void;
  keepGoing: () => void;
  close: () => void;
  askSkip: () => void;
  skip: () => void;
  onContinue: () => void;
}

const missingText = (view: View) => {
  const screen = SCREENS[view.index];
  return (screen.choice && !view.props.chosen) || !MISSING[screen.id] ? t('wizard.missing.choose') : MISSING[screen.id]!;
};

const DialogFooter: FunctionalComponent<{ view: View }> = ({ view }) => {
  if (view.mode === 'done')
    return (
      <Button variant="primary" onClick={() => route('/')}>
        {t('wizard.toDashboard')}
      </Button>
    );
  if (view.mode !== 'screen') return null;
  return (
    <Footer
      last={SCREENS[view.index].id === 'review'}
      answered={isAnswered(view.index, view.props)}
      missing={missingText(view)}
      busy={view.busy}
      // The next screen as the settings will be once this screen's edits are dropped.
      onSkip={() => view.go(nextScreen(view.index, { ...view.props, config: view.props.saved }))}
      onContinue={view.onContinue}
    />
  );
};

const DialogContent: FunctionalComponent<{ view: View; title: string }> = ({ view, title }) => {
  const { mode, props } = view;
  const screen = SCREENS[view.index];
  const Body = screen.Body;
  return (
    <div class="card-group" data-wizard-page={screen.page} data-wizard-screen={mode === 'screen' ? screen.id : mode}>
      <SpeechBubble id={QUESTION_ID} headingRef={view.heading}>
        {title}
      </SpeechBubble>
      <MotionNote status={props.status} />
      {mode === 'screen' && <Body {...props} />}
      {(mode === 'leave' || mode === 'skip') && (
        <LeaveChoices
          skipping={mode === 'skip'}
          busy={view.busy}
          keepGoing={view.keepGoing}
          leave={() => route(returnTo)}
          askSkip={view.askSkip}
          skip={view.skip}
        />
      )}
      {mode === 'done' && <DoneScreen />}
      {mode === 'done' && view.restart.length > 0 && <RestartNote restart={view.restart} />}
      {view.problem && (
        // Announced, and focused after a refused save (the busy Continue lost focus).
        <div role="alert" tabIndex={-1} id="wizard-problem-box" class="wizard-problem">
          <Note tone="bad" id="wizard-problem">
            {view.problem}
          </Note>
        </div>
      )}
    </div>
  );
};

// Called as a function, not a component: the loading screen's Modal and this
// one are then the same instance, so focus is captured once and given back once.
const wizardDialog = (view: View) => {
  const { mode, index, props } = view;
  const [title, why] = textsOf(mode, SCREENS[index].id);
  const previous = previousScreen(index, props);
  return (
    <Modal
      labelledBy={QUESTION_ID}
      onEscape={view.close}
      stopBar={<RoofStopBar />}
      header={
        <Header
          onBack={mode === 'screen' && previous >= 0 && !view.busy ? () => view.go(previous) : undefined}
          onClose={view.close}
          at={mode === 'done' ? [1, 1] : progressOf(index, props)}
        />
      }
      footer={mode === 'screen' || mode === 'done' ? <DialogFooter view={view} /> : undefined}
      aside={
        <>
          <h3>{t('wizard.whyTitle')}</h3>
          <p>{why}</p>
        </>
      }
    >
      <DialogContent view={view} title={title} key={`${index}-${mode}`} />
    </Modal>
  );
};

const Wizard: FunctionalComponent = () => {
  const editor = useConfigEditor();
  const { data: live } = useWebSocket<SystemStatus>('/ws/status');
  const { config, saved, setup, effective, loading, validationErrors, dirty } = editor;
  const [index, setIndex] = useState<number | null>(null);
  const seenUpTo = useRef(0);
  const [picked, setPicked] = useState<ReadonlySet<ScreenId>>(new Set());
  const [hallNotFitted, setHallNotFitted] = useState(false);
  const [alertPick, setAlertPick] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const [mode, setMode] = useState<Mode>('screen');
  const [restart, setRestart] = useState<string[]>([]);
  const heading = useQuestionFocus(`${index}-${mode}`);
  // The inputs as they were before the last save of a Test screen, so a Test
  // right after a plain Save still waits for a reading of the saved settings.
  const lastSave = useRef<{ index: number; before: RoofInputs | null; was: Config } | null>(null);
  // Password protection is offered once per visit (and only when none is saved).
  const passwordOffered = useRef(false);

  useEffect(markWizardOpened, []);
  useEffect(() => {
    if (loading || index !== null) return;
    const start = startScreen(setup);
    seenUpTo.current = start;
    setIndex(start);
  }, [loading, setup, index]);

  const leave = () => route(returnTo);
  const shell = (title: string, body: ComponentChildren) => (
    <Modal labelledBy={QUESTION_ID} onEscape={leave} stopBar={<RoofStopBar />} header={<Header onClose={leave} at={[1, SCREENS.length]} />}>
      <SpeechBubble id={QUESTION_ID} headingRef={heading}>
        {title}
      </SpeechBubble>
      {body}
    </Modal>
  );
  if (loading || (config && index === null)) return shell(t('settings.loadingSettings'), null);
  if (!config || !saved || index === null) return shell(t('settings.failedToLoadConfiguration'), null);

  const state = setup?.state ?? 'new';
  const status = live ?? editor.status;
  const screen = SCREENS[index];

  // Leaving a screen drops its unsaved edits: Back and Skip for now never
  // save, and no later save may carry them (spec 015 FR-007).
  // `justSaved`: the screen was saved this moment, so there is nothing to drop.
  const go = (to: number, justSaved = false) => {
    if (dirty && !justSaved) editor.discard();
    lastSave.current = null;
    setProblem(null);
    setMode('screen');
    setIndex(to);
  };

  // One save of the screen; false (with the reason shown) when it didn't go through.
  const save = async (next: SetupConfig | undefined): Promise<boolean> => {
    setBusy(true);
    const testScreen = screen.id === 'weatherAddress' || screen.id === 'nina';
    const before = testScreen ? await fetchInputs() : null;
    const was = saved;
    const outcome = await editor.save(next ? { setup: next } : {});
    if (testScreen && outcome.kind === 'saved') lastSave.current = { index, before, was };
    setBusy(false);
    setProblem(outcomeText(outcome));
    if (outcome.kind === 'invalid') focusField(outcome.errors);
    else if (outcome.kind !== 'saved') setTimeout(() => document.getElementById('wizard-problem-box')?.focus(), 0);
    if (outcome.kind === 'saved' && outcome.restart.length > 0) setRestart((prior) => [...new Set([...prior, ...outcome.restart])]);
    return outcome.kind === 'saved';
  };

  const saveForTest =
    (fields: string[]): TestSave =>
    async () => {
      const differs = (was: Config) => fields.some((field) => valueAt(was, field) !== valueAt(config, field));
      if (!dirty) {
        const last = lastSave.current?.index === index ? lastSave.current : null;
        return last ? { ok: true, changed: differs(last.was), config, before: last.before } : { ok: true, changed: false, config };
      }
      const changed = differs(saved);
      if (!(await save(undefined))) return { ok: false, message: t('wizard.test.notSaved') };
      return { ok: true, changed, config, before: lastSave.current?.before ?? null };
    };

  const chosen = state !== 'new' || index < seenUpTo.current || picked.has(screen.id);
  const props: WizardPageProps = {
    config,
    saved,
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
    alertPick,
    setAlertPick,
    saveForTest,
    saveHere: () => void save({ state, step: stepOf(index) }),
    // Keeps this screen's answer: it is saved first (a refused save stays here with its reason).
    goToPage: async (page) => {
      const to = settle(firstScreenOf(page), props);
      if (!dirty || (await save({ state, step: page }))) go(to, true);
    },
    offerPassword: () => {
      if (passwordOffered.current) return false;
      passwordOffered.current = true;
      return !saved.auth.enabled && saved.auth.password === '';
    },
    questionId: QUESTION_ID,
    chosen,
    choose: () => setPicked((prior) => new Set([...prior, screen.id])),
  };

  // A screen with changes is saved with the next screen's page; an unchanged
  // one only records the page (best effort: it has nothing to lose).
  const next = async () => {
    const to = nextScreen(index, props);
    const at: SetupConfig = { state, step: stepOf(to) };
    if (!dirty) {
      await saveSetup(at); // in order, so the device keeps the latest page
      return go(to, true);
    }
    if (await save(at)) go(to, true);
  };
  const finish = async () => {
    const done: SetupConfig = { state: 'done', step: 0 };
    if (dirty ? await save(done) : await saveSetup(done)) return setMode('done');
    if (!dirty) setProblem(t('wizard.finishFailed'));
  };
  const skip = async () => {
    setBusy(true);
    const ok = await saveSetup({ state: 'done', step: 0 });
    setBusy(false);
    if (!ok) {
      setTimeout(() => document.getElementById('wizard-problem-box')?.focus(), 0);
      return setProblem(t('wizard.skipFailed'));
    }
    showToast({ message: t('wizard.skipped') });
    route(returnTo);
  };

  const keepGoing = () => {
    setProblem(null);
    setMode('screen');
  };
  const view: View = {
    mode,
    index,
    props,
    busy,
    problem,
    restart,
    heading,
    go,
    keepGoing,
    // Escape and Close ask first (FR-004); on the leave screen Escape keeps going; once done, they close.
    close: () => (busy ? undefined : mode === 'done' ? route('/') : mode === 'screen' ? setMode('leave') : keepGoing()),
    askSkip: () => setMode('skip'),
    skip: () => void skip(),
    onContinue: () => void (screen.id === 'review' ? finish() : next()),
  };
  return wizardDialog(view);
};

export default Wizard;
