import { FunctionalComponent } from 'preact';
import type { BoardId, PinIssue, PinPreset, PinsConfig, PinsDocument } from '../../types';
import { PIN_FUNCTIONS, boardLabel, functionLabel, presetLabel, suggestedPreset, withPreset } from '../../lib/pinsView';
import { Field, NumberInput, SelectInput } from '../settings/controls';
import { ChoiceGroup, Note, ReadingRow, type Choice } from '../ui';
import { t } from '../../i18n';
import { deviceText } from '../../i18n/deviceMessage';

// The board and pin parts shared by the Pins page and the setup wizard's board
// screen (spec 023 FR-026, FR-027): the preset as choice cards, each pin, and
// the device's errors and warnings for the set as edited.

// `wt32`: the device has evidence it is a WT32-ETH01 (wt32Detected), so it refuses the DevKit's map.
export const presetChoices = (doc: PinsDocument | null, unset: boolean, wt32 = false): Choice[] => {
  const suggested = unset ? suggestedPreset(doc) : null;
  const badge = (preset: PinPreset) => (preset === suggested ? t('pins.suggested') : undefined);
  return [
    {
      value: 'devkit',
      icon: 'cpu',
      title: presetLabel('devkit'),
      detail: t('pins.presetDetail.devkit'),
      badge: wt32 ? undefined : badge('devkit'),
      unavailable: wt32 ? t('pins.devkitUnavailable') : undefined,
    },
    {
      value: 'wt32-carrier-v1',
      icon: 'link',
      title: presetLabel('wt32-carrier-v1'),
      detail: t('pins.presetDetail.wt32'),
      badge: badge('wt32-carrier-v1'),
    },
    { value: 'custom', icon: 'switch', title: presetLabel('custom'), detail: t('pins.presetDetail.custom') },
  ];
};

/** With no board chosen yet: nothing is driven, and which preset the boot probe suggests. */
export const UnsetNotes: FunctionalComponent<{ doc: PinsDocument | null }> = ({ doc }) => (
  <>
    <Note tone="warn">{t('pins.unsetNote')}</Note>
    <Note>{doc?.detected === 'wt32-eth01' ? t('pins.detectedWt32') : t('pins.detectedNone')}</Note>
  </>
);

export const PresetChoice: FunctionalComponent<{
  doc: PinsDocument | null;
  pins: PinsConfig;
  labelledBy: string;
  // No board saved yet: the boot probe's suggestion carries a badge.
  newDevice: boolean;
  // The device found a WT32-ETH01: the DevKit can't be chosen and Custom starts on the WT32.
  wt32: boolean;
  onChange: (pins: PinsConfig) => void;
}> = ({ doc, pins, labelledBy, newDevice, wt32, onChange }) => (
  <ChoiceGroup
    name="pins-preset"
    labelledBy={labelledBy}
    value={pins.preset === 'unset' ? null : pins.preset}
    onChange={(value) => onChange(withPreset(doc, pins, value as PinPreset, wt32))}
    options={presetChoices(doc, newDevice, wt32)}
  />
);

const gpio = (pin: number) => t('pins.gpio', { pin });

/** A named preset's pins, read-only. */
export const PinRows: FunctionalComponent<{ pins: PinsConfig }> = ({ pins }) => (
  <div data-pin-rows>
    <ReadingRow label={t('pins.board')} value={boardLabel(pins.board)} />
    {PIN_FUNCTIONS.map((fn) => (
      <ReadingRow key={fn} label={functionLabel(fn)} value={gpio(pins[fn])} />
    ))}
  </div>
);

const BOARDS: BoardId[] = ['esp32-devkit', 'wt32-eth01'];

// The element id of an issue's note, so the pin it is about can point at it.
const issueId = (prefix: string, issue: PinIssue) => `${prefix}-${issue.function ?? 'board'}-${issue.code}-${issue.pin}`;

/** Custom: the board and one number per function; the device checks the set and each pin points at its issues. */
export const CustomPins: FunctionalComponent<{
  pins: PinsConfig;
  issues: PinIssue[];
  issuePrefix: string;
  onChange: (pins: PinsConfig) => void;
}> = ({ pins, issues, issuePrefix, onChange }) => (
  <div class="form-grid" data-custom-pins>
    <Field label={t('pins.board')} hint={t('pins.boardHint')}>
      <SelectInput
        dataField="pins.board"
        value={pins.board}
        options={BOARDS.map((board) => ({ value: board, label: boardLabel(board) }))}
        onChange={(board) => onChange({ ...pins, board: board as BoardId })}
      />
    </Field>
    {PIN_FUNCTIONS.map((fn) => {
      const own = issues.filter((issue) => issue.function === fn);
      return (
        <Field key={fn} label={functionLabel(fn)}>
          <NumberInput
            dataField={`pins.${fn}`}
            integer
            min={0}
            value={pins[fn]}
            invalid={own.some((issue) => issue.error)}
            describedBy={own.map((issue) => issueId(issuePrefix, issue)).join(' ') || undefined}
            onChange={(pin) => onChange({ ...pins, [fn]: pin })}
          />
        </Field>
      );
    })}
  </div>
);

/**
 * The device's errors (a save is refused) and warnings (allowed) for the set as edited. A polite
 * live region: issues that arrive after the edit (the device is asked after a pause) are read out.
 */
export const IssueNotes: FunctionalComponent<{ issues: PinIssue[]; idPrefix: string }> = ({ issues, idPrefix }) => (
  <div data-pin-issues role="status" aria-live="polite">
    {issues.map((issue) => (
      <Note key={issueId(idPrefix, issue)} id={issueId(idPrefix, issue)} tone={issue.error ? 'bad' : 'warn'}>
        <span data-pin-issue={issue.function ?? 'board'} data-level={issue.error ? 'error' : 'warning'}>
          {deviceText(issue.text)}
        </span>
      </Note>
    ))}
  </div>
);
