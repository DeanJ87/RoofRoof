import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useEffect, useState } from 'preact/hooks';
import type { AnalogConfig, AnalogIssue, PinPreset, PinsConfig, PinsDocument, SystemStatus } from '../types';
import { useConfigEditor, type ConfigEditor, type SaveOutcome } from '../hooks/useConfigEditor';
import { usePinsDocument, usePinValidation } from '../hooks/usePins';
import { useWaitForReboot } from '../hooks/useWaitForReboot';
import { useWebSocket } from '../hooks/useWebSocket';
import { PIN_FUNCTIONS, errorsOf, functionLabel, presetLabel, suggestedPreset, withPreset, wt32Detected } from '../lib/pinsView';
import { announce } from '../lib/a11y';
import { getConfigValidationMessage } from '../validation/configSchema';
import { CustomPins, IssueNotes, PinRows, PresetChoice, UnsetNotes } from './pins/PinFields';
import { AdvancedSection } from './pins/AnalogFields';
import { analogErrors } from '../lib/analogView';
import { WiringCheckPanel } from './pins/WiringCheck';
import { MotionNote } from './settings/RoofTab';
import { SettingsCard, StatusBadge } from './settings/controls';
import { listReasons } from './settings/restart';
import { restartWithToast, showToast } from './toast';
import { Button, Note, ReadingRow } from './ui';
import { t } from '../i18n';
import { deviceText } from '../i18n/deviceMessage';

// The Pins page (spec 023 FR-026): the board and preset as choice cards, each
// function's pin (editable for Custom), the device's errors and warnings for
// the set as edited, the Advanced section (the analog front end and the park
// pull-ups), the pins in effect beside the saved ones with Restart, and the
// wiring check. The pins and the analog setup are saved together through the
// settings save like Settings; the pins apply at the next restart, the analog
// setup once the roof is still. Stop stays in the Layout's bar.

const PRESET_LABEL_ID = 'pins-preset-label';
const SAVE_BLOCKED_ID = 'pins-save-blocked';
const ISSUE_PREFIX = 'pins-issue';

const samePins = (a: PinsConfig | undefined, b: PinsConfig | undefined) => JSON.stringify(a) === JSON.stringify(b);
const sameAnalog = (a: AnalogConfig | undefined, b: AnalogConfig | undefined) => JSON.stringify(a) === JSON.stringify(b);

const outcomeText = (outcome: SaveOutcome): string | null => {
  if (outcome.kind === 'invalid') return getConfigValidationMessage(outcome.errors);
  if (outcome.kind === 'refused') return outcome.message;
  if (outcome.kind === 'unreachable') return t('settings.couldNotReachTheDevice');
  if (outcome.kind === 'conflict') return t('pins.changedOnDevice', { fields: listReasons(outcome.fields) });
  return null;
};

// Why Save is unavailable for the set as edited, or null.
const saveBlocked = (pins: PinsConfig, check: ReturnType<typeof usePinValidation>): string | null => {
  if (pins.preset === 'unset') return t('pins.chooseFirst');
  if (check.checking || (!check.validation && !check.problem)) return t('pins.checking');
  const validation = check.validation;
  if (validation && (errorsOf(validation.issues).length > 0 || analogErrors(validation.analogIssues ?? []).length > 0))
    return t('pins.saveBlocked');
  return check.problem;
};

// What the save note says applies when: the pins at the restart, the analog setup at once.
const savedText = (pinsChanged: boolean, analogChanged: boolean) => {
  if (pinsChanged && analogChanged) return t('pins.savedBoth');
  return analogChanged ? t('pins.analog.saved') : t('pins.saved');
};

// The device's checks on the analog setup as edited; its saved setup's while the device hasn't answered.
const analogIssuesOf = (doc: PinsDocument, check: ReturnType<typeof usePinValidation>, unchanged: boolean): AnalogIssue[] =>
  check.validation?.analogIssues ?? (unchanged ? (doc.analogIssues ?? []) : []);

// Save and Discard while something changed, and why Save is unavailable.
const SaveRow: FunctionalComponent<{
  dirty: boolean;
  blocked: string | null;
  busy: boolean;
  onSave: () => void;
  onDiscard: () => void;
}> = ({ dirty, blocked, busy, onSave, onDiscard }) => (
  <>
    {dirty && (
      <div class="btn-row">
        <Button
          variant="primary"
          onClick={onSave}
          disabled={blocked !== null}
          describedBy={blocked ? SAVE_BLOCKED_ID : undefined}
          busy={busy}
          busyLabel={t('common.saving')}
        >
          {t('pins.save')}
        </Button>
        <Button variant="ghost" onClick={onDiscard}>
          {t('settings.discard')}
        </Button>
      </div>
    )}
    {blocked && <Note id={SAVE_BLOCKED_ID}>{blocked}</Note>}
  </>
);

// The pins and analog setup as edited, what changed, and the device's verdict on them.
const useBoardEdit = (editor: ConfigEditor) => {
  const pins = editor.config!.pins!;
  const analog = editor.config!.analog;
  const check = usePinValidation(pins, analog);
  return {
    pins,
    saved: editor.saved?.pins,
    analog,
    check,
    issues: check.validation?.issues ?? [],
    pinsChanged: !samePins(pins, editor.saved?.pins),
    analogChanged: !sameAnalog(analog, editor.saved?.analog),
  };
};

const BoardCard: FunctionalComponent<{ editor: ConfigEditor; doc: PinsDocument; status: SystemStatus | null; onSaved: () => void }> = ({
  editor,
  doc,
  status,
  onSaved,
}) => {
  const { pins, saved, analog, check, issues, pinsChanged, analogChanged } = useBoardEdit(editor);
  const wt32 = wt32Detected(doc, status);
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const dirty = pinsChanged || analogChanged;
  const blocked = dirty ? saveBlocked(pins, check) : null;
  const change = (next: PinsConfig) => {
    setProblem(null);
    editor.applyChanges([[['pins'], next]]);
  };
  const changeAnalog = (next: AnalogConfig) => {
    setProblem(null);
    editor.applyChanges([[['analog'], next]]);
  };
  // A board preset chosen. The analog setup doesn't follow it: the pins apply at a restart and the
  // analog setup at once, and the board preset can't tell a carrier v1 from a v1.1 (its diode).
  const choose = (next: PinsConfig) => {
    setProblem(null);
    editor.applyChanges([[['pins'], next]]);
  };
  // A new device: the board its boot probe suggests is chosen for the person to confirm and save.
  useEffect(() => {
    if (saved?.preset === 'unset' && pins.preset === 'unset') choose(withPreset(doc, pins, suggestedPreset(doc)));
    // eslint-disable-next-line react-hooks/exhaustive-deps -- once, on a device with no board chosen
  }, []);
  const save = async () => {
    setBusy(true);
    const outcome = await editor.save();
    if (outcome.kind === 'conflict') await editor.rebase(outcome.fields);
    setBusy(false);
    const text = outcomeText(outcome);
    setProblem(text);
    // Read out a refused save: the busy Save button doesn't say it (the note is a live region too).
    if (text) announce(text);
    if (outcome.kind !== 'saved') return;
    showToast({ message: savedText(pinsChanged, analogChanged) });
    onSaved();
  };
  return (
    <SettingsCard id="pins" title={t('pins.boardTitle')} hint={t('pins.boardHint')}>
      {saved?.preset === 'unset' && <UnsetNotes doc={doc} />}
      <h3 class="card-group-title" id={PRESET_LABEL_ID}>
        {t('pins.presetLabel')}
      </h3>
      <PresetChoice
        doc={doc}
        pins={pins}
        labelledBy={PRESET_LABEL_ID}
        newDevice={saved?.preset === 'unset'}
        wt32={wt32}
        onChange={choose}
      />
      {pins.preset === 'custom' && <CustomPins pins={pins} issues={issues} issuePrefix={ISSUE_PREFIX} onChange={change} />}
      {pins.preset !== 'custom' && pins.preset !== 'unset' && <PinRows pins={pins} />}
      <IssueNotes issues={issues} idPrefix={ISSUE_PREFIX} />
      <AdvancedSection
        doc={doc}
        pins={pins}
        analog={analog}
        savedAnalog={editor.saved?.analog}
        issues={analogIssuesOf(doc, check, !analogChanged)}
        errors={editor.validationErrors}
        supplyVolts={status?.roof?.supplyVolts}
        onPins={change}
        onAnalog={changeAnalog}
      />
      <div role="status" aria-live="polite" data-pins-problem>
        {problem && <Note tone="bad">{problem}</Note>}
      </div>
      <SaveRow dirty={dirty} blocked={blocked} busy={busy} onSave={() => void save()} onDiscard={editor.discard} />
    </SettingsCard>
  );
};

const gpio = (pin: number) => t('pins.gpio', { pin });

const validityBadge = (doc: PinsDocument) => {
  if (doc.running.preset === 'unset') return <StatusBadge tone="off" label={t('pins.status.unset')} />;
  return doc.valid ? <StatusBadge tone="ok" label={t('pins.status.valid')} /> : <StatusBadge tone="bad" label={t('pins.status.invalid')} />;
};

// What the device uses now beside what is saved, and Restart when they differ.
const InEffectCard: FunctionalComponent<{ doc: PinsDocument; busy: boolean; onRestart: () => void }> = ({ doc, busy, onRestart }) => {
  const { running, saved } = doc;
  const restart = doc.restartNeeded || !samePins(running, saved);
  return (
    <SettingsCard id="pins-in-effect" title={t('pins.inEffect')} hint={t('pins.inEffectHint')} badge={validityBadge(doc)}>
      {!doc.valid && doc.reason && <Note tone="bad">{deviceText(doc.reason)}</Note>}
      <ReadingRow
        label={t('pins.presetLabel')}
        value={
          running.preset === saved.preset
            ? presetLabel(running.preset)
            : t('pins.runningSaved', { running: presetLabel(running.preset), saved: presetLabel(saved.preset) })
        }
      />
      {running.preset !== 'unset' &&
        PIN_FUNCTIONS.map((fn) => (
          <ReadingRow
            key={fn}
            label={functionLabel(fn)}
            value={
              running[fn] === saved[fn] ? gpio(running[fn]) : t('pins.runningSaved', { running: gpio(running[fn]), saved: gpio(saved[fn]) })
            }
          />
        ))}
      {restart && (
        <div data-pins-restart>
          <Note tone="warn">{t('pins.restartNote')}</Note>
          <div class="btn-row">
            <Button variant="danger" onClick={onRestart} busy={busy} busyLabel={t('common.restarting')}>
              {t('pins.restart')}
            </Button>
          </div>
        </div>
      )}
    </SettingsCard>
  );
};

const Pins: FunctionalComponent = () => {
  const editor = useConfigEditor();
  const { doc, loading, reload } = usePinsDocument();
  const { data: live } = useWebSocket<SystemStatus>('/ws/status');
  const [restarting, setRestarting] = useState(false);
  useWaitForReboot(restarting, () => {
    setRestarting(false);
    reload();
    editor.reload();
  });
  if (editor.loading || loading) return <div class="empty-state">{t('settings.loadingSettings')}</div>;
  const pins = editor.config?.pins;
  if (!pins || !doc) return <div class="empty-state tone-red">{t('pins.couldNotLoad')}</div>;
  // The wiring check's suggested preset (the analog setup stays, as on the cards).
  const choosePreset = (preset: PinPreset) => editor.applyChanges([[['pins'], withPreset(doc, pins, preset)]]);
  // Waits for the device to come back only once it accepted the restart; a refusal says why.
  const restart = () => void restartWithToast().then((accepted) => accepted && setRestarting(true));
  return (
    <div class="panel-page settings-page page-enter" data-pins-page>
      <h2>{t('pins.title')}</h2>
      <MotionNote status={live ?? editor.status} />
      <BoardCard editor={editor} doc={doc} status={live ?? editor.status} onSaved={reload} />
      <InEffectCard doc={doc} busy={restarting} onRestart={restart} />
      <SettingsCard id="wiring" title={t('pins.check.title')} hint={t('pins.check.hint')}>
        <WiringCheckPanel doc={doc} onChoosePreset={choosePreset} />
      </SettingsCard>
      <div class="btn-row">
        <Button variant="link" onClick={() => route('/settings?tab=roof&section=pins')}>
          {t('pins.toSettings')}
        </Button>
      </div>
    </div>
  );
};

export default Pins;
