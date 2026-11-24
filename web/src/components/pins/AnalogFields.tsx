import { FunctionalComponent } from 'preact';
import { useRef, useState } from 'preact/hooks';
import type { AnalogConfig, AnalogInput, AnalogIssue, AnalogPreset, Divider, PinsConfig, PinsDocument, Pullup } from '../../types';
import { ANALOG_INPUTS, SENSOR_MAX_VOLTS, calibrateSupplyGain, dividerRatio, pinVoltsAtMax, withAnalogPreset } from '../../lib/analogView';
import type { ValidationErrors } from '../../validation/configSchema';
import { Field, Fold, NumberInput, SelectInput } from '../settings/controls';
import { Button, ChoiceGroup, Note, ReadingRow, type Choice } from '../ui';
import { t, type MessageKey } from '../../i18n';
import { formatInputNumber, formatNumber } from '../../i18n/format';
import { deviceText } from '../../i18n/deviceMessage';

// The Pins page's Advanced section (spec 023): the analog front end (each input's
// divider and trim, as a named preset or Custom), the device's checks on it,
// calibrating the supply to a measured voltage, and the park inputs' pull-ups.
// Saved with the pins (one settings save); analog changes apply once the roof is
// still, the pull-ups at the next restart.

const PRESET_LABEL_ID = 'analog-preset-label';
const ISSUE_PREFIX = 'analog-issue';

const PRESET_KEY: Record<AnalogPreset, [MessageKey, MessageKey]> = {
  'devkit-bench': ['pins.analog.preset.devkit', 'pins.analog.presetDetail.devkit'],
  'wt32-carrier-v1.1': ['pins.analog.preset.wt32', 'pins.analog.presetDetail.wt32'],
  custom: ['pins.analog.preset.custom', 'pins.analog.presetDetail.custom'],
};
const PRESET_ICON: Record<AnalogPreset, string> = { 'devkit-bench': 'cpu', 'wt32-carrier-v1.1': 'link', custom: 'switch' };
const presetChoices = (): Choice[] =>
  (Object.keys(PRESET_KEY) as AnalogPreset[]).map((preset) => ({
    value: preset,
    icon: PRESET_ICON[preset],
    title: t(PRESET_KEY[preset][0]),
    detail: t(PRESET_KEY[preset][1]),
  }));

const inputLabel = (input: AnalogInput) => t(`pins.fn.${input}`);
const volts = (value: number | null) => (value === null ? '--' : t('pins.analog.volts', { value: formatNumber(value, 2) }));

/** A resistor's value as read: 100 Ω, 4.7 kΩ, 22 kΩ, 1 MΩ (whole ohms, so exact). */
export const formatOhms = (ohms: number): string => {
  if (!Number.isFinite(ohms)) return '--';
  if (ohms >= 1e6) return t('pins.analog.megohms', { value: formatInputNumber(ohms / 1e6) });
  if (ohms >= 1e3) return t('pins.analog.kilohms', { value: formatInputNumber(ohms / 1e3) });
  return t('pins.analog.ohms', { value: formatInputNumber(ohms) });
};

// An issue's note id, so the input it is about can point at it.
const issueId = (issue: AnalogIssue, index: number) => `${ISSUE_PREFIX}-${issue.input || 'all'}-${index}`;

/** The device's errors (a save is refused) and warnings (allowed) on the analog setup; read out when they arrive. */
export const AnalogIssueNotes: FunctionalComponent<{ issues: AnalogIssue[] }> = ({ issues }) => (
  <div data-analog-issues role="status" aria-live="polite">
    {issues.map((issue, index) => (
      <Note key={issueId(issue, index)} id={issueId(issue, index)} tone={issue.error ? 'bad' : 'warn'}>
        <span data-analog-issue={issue.input || 'all'} data-level={issue.error ? 'error' : 'warning'}>
          {deviceText(issue.text)}
        </span>
      </Note>
    ))}
  </div>
);

type Trim = { gain: number; offset: number; offsetKey: 'offsetVolts' | 'offsetAmps'; unit: string };
const trimOf = (analog: AnalogConfig, input: AnalogInput): Trim | null => {
  if (input === 'supply')
    return { gain: analog.supply.gain, offset: analog.supply.offsetVolts, offsetKey: 'offsetVolts', unit: t('pins.analog.unitVolts') };
  if (input === 'hall') return null;
  return { gain: analog[input].gain, offset: analog[input].offsetAmps, offsetKey: 'offsetAmps', unit: t('pins.analog.unitAmps') };
};

/** One input: its divider and trim (editable for Custom), the ratio and the pin's voltage at the sensor's maximum. */
const AnalogInputRows: FunctionalComponent<{
  analog: AnalogConfig;
  input: AnalogInput;
  issues: AnalogIssue[];
  errors: ValidationErrors;
  onChange: (analog: AnalogConfig) => void;
}> = ({ analog, input, issues, errors, onChange }) => {
  const divider: Divider = analog[input];
  const trim = trimOf(analog, input);
  const custom = analog.preset === 'custom';
  const own = issues.map((issue, index) => ({ issue, index })).filter(({ issue }) => issue.input === input);
  const describedBy = own.map(({ issue, index }) => issueId(issue, index)).join(' ') || undefined;
  const invalid = own.some(({ issue }) => issue.error);
  const set = (key: string, value: number) => onChange({ ...analog, [input]: { ...analog[input], [key]: value } });
  const ratio = dividerRatio(divider);
  const ohms = t('pins.analog.unitOhms');
  const error = (key: string) => errors[`analog.${input}.${key}`];
  const number = (key: string, label: string, value: number, unit: string | undefined, integer: boolean) => (
    <Field label={label} error={error(key)}>
      <NumberInput
        dataField={`analog.${input}.${key}`}
        value={value}
        integer={integer}
        min={integer ? 0 : undefined}
        unit={unit}
        error={error(key)}
        invalid={invalid}
        describedBy={describedBy}
        onChange={(next) => set(key, next)}
      />
    </Field>
  );
  return (
    <div class="analog-input" data-analog-input={input}>
      <h4 class="card-group-title">{inputLabel(input)}</h4>
      {custom ? (
        <div class="form-grid">
          {number('top', t('pins.analog.top'), divider.top, ohms, true)}
          {number('bottom', t('pins.analog.bottom'), divider.bottom, ohms, true)}
          {trim && number('gain', t('pins.analog.gain'), trim.gain, undefined, false)}
          {trim && number(trim.offsetKey, t('pins.analog.offset'), trim.offset, trim.unit, false)}
        </div>
      ) : (
        <>
          <ReadingRow label={t('pins.analog.top')} value={formatOhms(divider.top)} />
          <ReadingRow label={t('pins.analog.bottom')} value={formatOhms(divider.bottom)} />
          {trim && <ReadingRow label={t('pins.analog.gain')} value={formatNumber(trim.gain, 3)} />}
          {trim && <ReadingRow label={t('pins.analog.offset')} value={`${formatNumber(trim.offset, 2)} ${trim.unit}`} />}
        </>
      )}
      <ReadingRow
        label={t('pins.analog.ratio')}
        value={ratio === null ? '--' : t('pins.analog.times', { value: formatNumber(ratio, 3) })}
      />
      <ReadingRow
        label={t('pins.analog.atPin', { max: formatNumber(SENSOR_MAX_VOLTS[input], 1) })}
        value={volts(pinVoltsAtMax(input, divider))}
      />
    </div>
  );
};

const CALIBRATE_REFUSAL: Record<'noReading' | 'measured' | 'range', MessageKey> = {
  noReading: 'pins.analog.calibrate.noReading',
  measured: 'pins.analog.calibrate.measuredLow',
  range: 'pins.analog.calibrate.outOfRange',
};

/**
 * Calibrate the supply to what a multimeter reads: sets the gain (as Custom) for the person to save.
 * The reading was scaled with the `saved` setup, so the gain is worked out from it for the setup as edited.
 */
const CalibrateSupply: FunctionalComponent<{
  analog: AnalogConfig;
  saved: AnalogConfig;
  supplyVolts: number | null | undefined;
  onChange: (analog: AnalogConfig) => void;
}> = ({ analog, saved, supplyVolts, onChange }) => {
  const [measured, setMeasured] = useState(Number.NaN);
  const [result, setResult] = useState<{ ok: boolean; text: string } | null>(null);
  const calibrate = () => {
    const answer = calibrateSupplyGain(saved.supply, analog.supply, supplyVolts, measured);
    if (!answer.ok) return setResult({ ok: false, text: t(CALIBRATE_REFUSAL[answer.reason]) });
    onChange({ ...analog, preset: 'custom', supply: { ...analog.supply, gain: answer.gain } });
    setResult({ ok: true, text: t('pins.analog.calibrate.done', { gain: formatNumber(answer.gain, 4) }) });
  };
  const reading = typeof supplyVolts === 'number' && Number.isFinite(supplyVolts) ? volts(supplyVolts) : t('pins.analog.calibrate.noValue');
  return (
    <div class="analog-calibrate" data-analog-calibrate>
      <h4 class="card-group-title">{t('pins.analog.calibrate.title')}</h4>
      <ReadingRow label={t('pins.analog.calibrate.reading')} value={reading} />
      <div class="form-grid">
        <Field label={t('pins.analog.calibrate.measured')} hint={t('pins.analog.calibrate.hint')}>
          <NumberInput
            dataField="analog.calibrate.measured"
            value={measured}
            min={0}
            unit={t('pins.analog.unitVolts')}
            onChange={setMeasured}
          />
        </Field>
      </div>
      <div class="btn-row">
        <Button onClick={calibrate} disabled={!Number.isFinite(measured)}>
          {t('pins.analog.calibrate.apply')}
        </Button>
      </div>
      <div role="status" aria-live="polite">
        {result && <Note tone={result.ok ? 'ok' : 'bad'}>{result.text}</Note>}
      </div>
    </div>
  );
};

const PULLUPS: ['parkA' | 'parkB', 'parkAPullup' | 'parkBPullup'][] = [
  ['parkA', 'parkAPullup'],
  ['parkB', 'parkBPullup'],
];

/** Each park input's pull-up: the chip's own, or one fitted on the board (needed on GPIO34-39). */
const PullupFields: FunctionalComponent<{ pins: PinsConfig; onChange: (pins: PinsConfig) => void }> = ({ pins, onChange }) => (
  <div data-pullups>
    <h4 class="card-group-title">{t('pins.pullup.title')}</h4>
    <div class="form-grid">
      {PULLUPS.map(([fn, key]) => (
        <Field key={key} label={t(`pins.fn.${fn}`)} hint={t('pins.pullup.hint')}>
          <SelectInput
            dataField={`pins.${key}`}
            value={pins[key] ?? 'internal'}
            options={[
              { value: 'internal', label: t('pins.pullup.internal') },
              { value: 'external', label: t('pins.pullup.external') },
            ]}
            onChange={(value) => onChange({ ...pins, [key]: value as Pullup })}
          />
        </Field>
      ))}
    </div>
    <Note>{t('pins.pullup.note')}</Note>
  </div>
);

/**
 * The Advanced fold: closed until opened, and opened (for good) once the analog setup has an
 * error, so what blocks Save is in view.
 */
export const AdvancedSection: FunctionalComponent<{
  doc: PinsDocument;
  pins: PinsConfig;
  analog: AnalogConfig | undefined;
  // The saved analog setup: the one that scaled the live supply reading.
  savedAnalog: AnalogConfig | undefined;
  issues: AnalogIssue[];
  errors: ValidationErrors;
  supplyVolts: number | null | undefined;
  onPins: (pins: PinsConfig) => void;
  onAnalog: (analog: AnalogConfig) => void;
}> = ({ doc, pins, analog, savedAnalog, issues, errors, supplyVolts, onPins, onAnalog }) => {
  const hasError = issues.some((issue) => issue.error) || Object.keys(errors).some((key) => key.startsWith('analog.'));
  const opened = useRef(false);
  if (hasError) opened.current = true;
  return (
    <Fold label={t('pins.advanced.title')} open={opened.current || undefined} dataField="pins.advanced">
      <Note>{t('pins.advanced.applyNote')}</Note>
      {analog && (
        <div data-analog>
          <h3 class="card-group-title" id={PRESET_LABEL_ID}>
            {t('pins.analog.presetLabel')}
          </h3>
          <ChoiceGroup
            name="analog-preset"
            labelledBy={PRESET_LABEL_ID}
            value={analog.preset}
            onChange={(value) => onAnalog(withAnalogPreset(doc, analog, value as AnalogPreset))}
            options={presetChoices()}
          />
          {analog.preset !== 'custom' && <Note>{t('pins.analog.namedNote')}</Note>}
          {ANALOG_INPUTS.map((input) => (
            <AnalogInputRows key={input} analog={analog} input={input} issues={issues} errors={errors} onChange={onAnalog} />
          ))}
          <AnalogIssueNotes issues={issues} />
          <CalibrateSupply analog={analog} saved={savedAnalog ?? analog} supplyVolts={supplyVolts} onChange={onAnalog} />
        </div>
      )}
      <PullupFields pins={pins} onChange={onPins} />
    </Fold>
  );
};
