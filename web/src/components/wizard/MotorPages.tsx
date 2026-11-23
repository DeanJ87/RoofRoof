import { FunctionalComponent, type ComponentChildren } from 'preact';
import type { RoofConfig, RoofStatus } from '../../types';
import type { WizardPageProps } from './context';
import TeachCard, { JogPanel } from '../calibrate/TeachCard';
import { Checks, Result, Running, StartForm, jogGate } from '../Calibrate';
import { useCalibration } from '../../hooks/useCalibration';
import { useTeach } from '../../hooks/useTeach';
import { abortCalibration } from '../../lib/calibrationApi';
import { listReasons } from '../settings/restart';
import { Button, ChoiceGroup, Note } from '../ui';
import { t } from '../../i18n';

// The motor part (spec 025): spec 019's attended jog and teach and spec 016's
// calibration run, the Calibrate page's own components inside the dialog. Every
// gate stays: the person confirms they are at the roof on each screen, the park
// and weather checks (or a test box bypass, named here) and every fault check
// apply, and the device checks again when a move starts. The Stop bar is in
// the dialog while the roof moves.

type Motor = { roof: RoofStatus; config: RoofConfig };

const BYPASS_NAMES = [
  ['park', 'testBox.banner.park'],
  ['weather', 'testBox.banner.weather'],
  ['daylight', 'testBox.banner.daylight'],
] as const;

// FR-014: a test box bypass in effect is named on every motor screen.
const BypassNote: FunctionalComponent<{ roof: RoofStatus }> = ({ roof }) => {
  const box = roof.testBox;
  const names = BYPASS_NAMES.filter(([key]) => box?.active && box[key]).map(([, label]) => t(label));
  return names.length > 0 ? <Note tone="warn">{t('wizard.motor.bypassed', { checks: listReasons(names) })}</Note> : null;
};

// What every motor screen has around its own part: the bypass note and "Enter manually instead".
const MotorFrame: FunctionalComponent<WizardPageProps & { render: (motor: Motor) => ComponentChildren }> = (props) => {
  const roof = props.status?.roof;
  const config = props.saved.roof;
  return (
    <div class="card-group" data-motor>
      {roof && config ? (
        <>
          <BypassNote roof={roof} />
          {props.render({ roof, config })}
        </>
      ) : (
        <Note>{t('wizard.noReading')}</Note>
      )}
      <div class="btn-row">
        <Button variant="ghost" onClick={() => void props.goToScreen('motion')}>
          {t('wizard.motor.manual')}
        </Button>
      </div>
    </div>
  );
};

// The wrong way: reverse the opening direction here (FR-007), save it, then hold Open again.
const WrongWay: FunctionalComponent<WizardPageProps & { roof: RoofConfig }> = (props) => {
  const reverse = () => props.update(['roof', 'openDirection'], props.roof.openDirection === 'forward' ? 'reverse' : 'forward');
  return props.dirty ? (
    <Note tone="warn" action={{ label: t('settings.save'), onClick: props.saveHere }}>
      {t('wizard.motor.saveDirection')}
    </Note>
  ) : (
    <Note tone="warn" action={{ label: t('wizard.motor.reverse'), onClick: reverse }}>
      {t('wizard.motor.wrongWay')}
    </Note>
  );
};

// Does holding Open open the roof? The first short move, attended (FR-010).
export const DirectionScreen: FunctionalComponent<WizardPageProps> = (props) => {
  // Only what the person saw in this visit (FR-005): never chosen for them, also on a re-run.
  const choice = {
    name: 'direction',
    labelledBy: props.questionId,
    value: props.direction,
    onChange: (v: string) => props.setDirection(v as 'yes' | 'no'),
  };
  return (
    <MotorFrame
      {...props}
      render={({ roof, config }) => {
        const gate = jogGate(roof, props.fresh);
        return (
          <>
            <JogPanel roof={roof} config={config} checks={<Checks checks={gate.checks} />} blocked={gate.blocked} />
            <ChoiceGroup
              {...choice}
              options={[
                { value: 'yes', icon: 'roof', title: t('wizard.motor.opened'), detail: t('wizard.motor.openedDetail') },
                { value: 'no', icon: 'off', title: t('wizard.motor.closed'), detail: t('wizard.motor.closedDetail') },
              ]}
            />
            {choice.value === 'no' && <WrongWay {...props} roof={config} />}
          </>
        );
      }}
    />
  );
};

// Mark the closed end, part-way and the open end; confirm the proposed Hall settings (FR-011).
export const TeachScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { status: teach, refresh } = useTeach();
  const { status: run } = useCalibration();
  return (
    <MotorFrame
      {...props}
      render={({ roof, config }) => {
        // As the Calibrate page: no jog while a run is under way (the device refuses one anyway).
        if (run?.active) return <Note tone="warn">{t('wizard.motor.runActive')}</Note>;
        const gate = jogGate(roof, props.fresh);
        return (
          <TeachCard
            plain
            teach={teach}
            roof={roof}
            config={config}
            checks={<Checks checks={gate.checks} />}
            blocked={gate.blocked}
            onChanged={() => {
              refresh();
              props.reload();
            }}
          />
        );
      }}
    />
  );
};

// The calibration run, its open-end question and its proposal (FR-012).
export const CalibrationScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { status: run, refresh } = useCalibration();
  return (
    <MotorFrame
      {...props}
      render={({ roof, config }) => (
        <>
          {run?.active ? (
            <Running plain run={run} roof={roof} config={config} onStop={() => void abortCalibration().then(refresh)} onAnswer={refresh} />
          ) : (
            <StartForm plain roof={roof} config={config} live={props.fresh} onStarted={refresh} />
          )}
          {run && !run.active && run.state !== 'idle' && <Result plain run={run} config={config} accept={props.acceptRoof} />}
        </>
      )}
    />
  );
};
