import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useEffect, useState } from 'preact/hooks';
import type { PinsConfig, PinsDocument, SystemStatus } from '../types';
import { useConfigEditor, type ConfigEditor, type SaveOutcome } from '../hooks/useConfigEditor';
import { usePinsDocument, usePinValidation } from '../hooks/usePins';
import { useWaitForReboot } from '../hooks/useWaitForReboot';
import { useWebSocket } from '../hooks/useWebSocket';
import { restartDevice } from '../lib/settingsApi';
import { PIN_FUNCTIONS, errorsOf, functionLabel, presetLabel, suggestedPreset, withPreset } from '../lib/pinsView';
import { getConfigValidationMessage } from '../validation/configSchema';
import { CustomPins, IssueNotes, PinRows, PresetChoice, UnsetNotes } from './pins/PinFields';
import { WiringCheckPanel } from './pins/WiringCheck';
import { MotionNote } from './settings/RoofTab';
import { SettingsCard, StatusBadge } from './settings/controls';
import { listReasons } from './settings/restart';
import { showToast } from './toast';
import { Button, Note, ReadingRow } from './ui';
import { t } from '../i18n';
import { deviceText } from '../i18n/deviceMessage';

// The Pins page (spec 023 FR-026): the board and preset as choice cards, each
// function's pin (editable for Custom), the device's errors and warnings for
// the set as edited, the pins in effect beside the saved ones with Restart,
// and the wiring check. The pins are saved through the settings save like
// Settings; they apply at the next restart. Stop stays in the Layout's bar.

const PRESET_LABEL_ID = 'pins-preset-label';
const SAVE_BLOCKED_ID = 'pins-save-blocked';

const samePins = (a: PinsConfig | undefined, b: PinsConfig | undefined) => JSON.stringify(a) === JSON.stringify(b);

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
  if (check.validation && errorsOf(check.validation.issues).length > 0) return t('pins.saveBlocked');
  return check.problem;
};

const BoardCard: FunctionalComponent<{ editor: ConfigEditor; doc: PinsDocument; onSaved: () => void }> = ({ editor, doc, onSaved }) => {
  const pins = editor.config!.pins!;
  const saved = editor.saved?.pins;
  const check = usePinValidation(pins);
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const dirty = !samePins(pins, saved);
  const blocked = dirty ? saveBlocked(pins, check) : null;
  const change = (next: PinsConfig) => {
    setProblem(null);
    editor.applyChanges([[['pins'], next]]);
  };
  // A new device: the board its boot probe suggests is chosen for the person to confirm and save.
  useEffect(() => {
    if (saved?.preset === 'unset' && pins.preset === 'unset') change(withPreset(doc, pins, suggestedPreset(doc)));
    // eslint-disable-next-line react-hooks/exhaustive-deps -- once, on a device with no board chosen
  }, []);
  const save = async () => {
    setBusy(true);
    const outcome = await editor.save();
    if (outcome.kind === 'conflict') await editor.rebase(outcome.fields);
    setBusy(false);
    setProblem(outcomeText(outcome));
    if (outcome.kind !== 'saved') return;
    showToast({ message: t('pins.saved') });
    onSaved();
  };
  return (
    <SettingsCard id="pins" title={t('pins.boardTitle')} hint={t('pins.boardHint')}>
      {saved?.preset === 'unset' && <UnsetNotes doc={doc} />}
      <h3 class="card-group-title" id={PRESET_LABEL_ID}>
        {t('pins.presetLabel')}
      </h3>
      <PresetChoice doc={doc} pins={pins} labelledBy={PRESET_LABEL_ID} newDevice={saved?.preset === 'unset'} onChange={change} />
      {pins.preset === 'custom' && <CustomPins pins={pins} onChange={change} />}
      {pins.preset !== 'custom' && pins.preset !== 'unset' && <PinRows pins={pins} />}
      {check.validation && <IssueNotes issues={check.validation.issues} />}
      {problem && <Note tone="bad">{problem}</Note>}
      {dirty && (
        <div class="btn-row">
          <Button
            variant="primary"
            onClick={() => void save()}
            disabled={blocked !== null}
            describedBy={blocked ? SAVE_BLOCKED_ID : undefined}
            busy={busy}
            busyLabel={t('common.saving')}
          >
            {t('pins.save')}
          </Button>
          <Button variant="ghost" onClick={editor.discard}>
            {t('settings.discard')}
          </Button>
        </div>
      )}
      {blocked && <Note id={SAVE_BLOCKED_ID}>{blocked}</Note>}
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
  const restart = () =>
    void restartDevice()
      .then(() => {
        showToast({ message: t('common.restarting') });
        setRestarting(true);
      })
      .catch(() => showToast({ message: t('settings.couldNotReachTheDevice'), tone: 'bad' }));
  return (
    <div class="panel-page settings-page page-enter" data-pins-page>
      <h2>{t('pins.title')}</h2>
      <MotionNote status={live ?? editor.status} />
      <BoardCard editor={editor} doc={doc} onSaved={reload} />
      <InEffectCard doc={doc} busy={restarting} onRestart={restart} />
      <SettingsCard id="wiring" title={t('pins.check.title')} hint={t('pins.check.hint')}>
        <WiringCheckPanel doc={doc} onChoosePreset={(preset) => editor.applyChanges([[['pins'], withPreset(doc, pins, preset)]])} />
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
