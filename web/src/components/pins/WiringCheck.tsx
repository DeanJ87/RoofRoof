import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import type { PinsDocument, WiringCheckResult } from '../../types';
import { useWiringCheck } from '../../hooks/usePins';
import { LEVEL_PILL, checkBlocked } from '../../lib/pinsView';
import { Button, Note, Pill } from '../ui';
import { t } from '../../i18n';
import { deviceText } from '../../i18n/deviceMessage';
import { formatNumber } from '../../i18n/format';

// The wiring check (spec 023 FR-015 to FR-019): reads each input and the EN
// pull-down without moving the roof. On the Pins page and the setup wizard's
// Check wiring screen. Direction and travel need motion: spec 019's Jog and teach.

const NOTE_TONE = { pass: 'ok', warn: 'warn', fail: 'bad', skipped: 'muted' } as const;
const BLOCKED_ID = 'wiring-check-blocked';

const valueText = (check: WiringCheckResult) =>
  check.value === null ? '' : `${formatNumber(check.value, Number.isInteger(check.value) ? 0 : 1)}${check.unit ? ` ${check.unit}` : ''}`;

const ResultRow: FunctionalComponent<{ check: WiringCheckResult }> = ({ check }) => (
  <div data-wiring-check={check.id}>
    <div class="reading-row">
      <span class="reading-label">{t(`pins.check.id.${check.id}`)}</span>
      <span class="reading-value">
        {valueText(check) && <span class="mono ltr">{valueText(check)} </span>}
        <Pill tone={LEVEL_PILL[check.level]}>{t(`pins.check.level.${check.level}`)}</Pill>
      </span>
    </div>
    {check.text && <Note tone={NOTE_TONE[check.level]}>{deviceText(check.text)}</Note>}
  </div>
);

export interface WiringCheckPanelProps {
  doc: PinsDocument | null;
  // Offered when the check found Ethernet on a board set up without it.
  onChoosePreset?: (preset: 'wt32-carrier-v1') => void;
}

export const WiringCheckPanel: FunctionalComponent<WiringCheckPanelProps> = ({ doc, onChoosePreset }) => {
  const { check, running, problem, start } = useWiringCheck();
  const blocked = checkBlocked(doc);
  const results = check?.state === 'done' ? check.checks.filter((result) => result.id !== 'motion') : [];
  return (
    <div class="card-group" data-wiring-panel>
      <div class="btn-row">
        <Button
          onClick={() => void start()}
          disabled={blocked !== null}
          describedBy={blocked ? BLOCKED_ID : undefined}
          busy={running}
          busyLabel={t('pins.check.running')}
        >
          {t('pins.check.start')}
        </Button>
      </div>
      {blocked && <Note id={BLOCKED_ID}>{blocked}</Note>}
      {problem && <Note tone="bad">{problem}</Note>}
      {check?.state === 'idle' && !blocked && <Note>{t('pins.check.idle')}</Note>}
      {results.map((result) => (
        <ResultRow key={result.id} check={result} />
      ))}
      {check?.suggestPreset && onChoosePreset && (
        <Note tone="warn" action={{ label: t('pins.check.usePreset'), onClick: () => onChoosePreset('wt32-carrier-v1') }}>
          {t('pins.check.suggest')}
        </Note>
      )}
      <Note action={{ label: t('pins.check.openTeach'), onClick: () => route('/calibrate') }}>{t('pins.check.motion')}</Note>
    </div>
  );
};

export default WiringCheckPanel;
