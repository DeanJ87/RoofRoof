import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import type { Stage, StageState } from '../lib/setupProgress';
import { Button, Note, Pill } from './ui';
import { t } from '../i18n';

// Spec 026: the roof's setup stages (lib/setupProgress.ts), each a row with its state
// and the page that does it; a stage not done says what is left on its own line.
// On the dashboard (its own card while the roof isn't set up) and in Settings → Roof.

const PILL: Record<StageState, string> = { done: 'pill-green', todo: 'pill-amber', typed: 'pill-amber', bypassed: 'pill-amber' };

export const SetupStages: FunctionalComponent<{ stages: Stage[] }> = ({ stages }) => (
  <div data-setup-stages>
    {stages.map((stage, index) => (
      <div key={stage.id} data-stage={stage.id} data-state={stage.state}>
        <div class="reading-row">
          <Button variant="link" onClick={() => route(stage.href)}>
            {`${index + 1}. ${stage.title}`}
          </Button>
          <Pill tone={PILL[stage.state]}>{t(`stage.state.${stage.state}`)}</Pill>
        </div>
        {stage.state !== 'done' && <Note tone={stage.state === 'bypassed' ? 'warn' : 'muted'}>{stage.note}</Note>}
      </div>
    ))}
  </div>
);

/** "4 of 7 done", for the card's pill. */
export const stagesDone = (stages: Stage[]) =>
  t('stage.count', { done: stages.filter((stage) => stage.state === 'done').length, total: stages.length });
