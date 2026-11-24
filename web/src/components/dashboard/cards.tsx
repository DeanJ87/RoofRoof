import type { Config, SystemStatus } from '../../types';
import type { MasonryItem } from '../Masonry';
import DeviceCard from '../../dashboard/DeviceCard';
import RoofCard from '../../dashboard/RoofCard';
import type { LiveState } from '../../lib/roofView';
import { Card, Pill } from '../ui';
import { SetupStages, stagesDone } from '../SetupProgress';
import { setupComplete, setupStages } from '../../lib/setupProgress';
import { t } from '../../i18n';

// What the dashboard's cards are drawn from.
export interface CardContext {
  status: SystemStatus | null;
  config?: Config | null;
  // The live data's state, for the Roof card's buttons (spec 009 FR-004).
  live?: LiveState;
}

// Every card there is, in no particular order (the order is the user's). The
// Status card is added by Dashboard.tsx.
export const dashboardCards = (ctx: CardContext): MasonryItem[] => [
  {
    id: 'roof',
    title: t('roof.title'),
    node: (
      <RoofCard
        live={ctx.live ?? { status: ctx.status, connected: false, quiet: false, reconnectedAt: null }}
        config={ctx.config?.roof}
        confirm={ctx.config?.ui?.confirmOpenClose ?? false}
        rearmMinutes={ctx.config?.autoClose?.rearmMinutes}
      />
    ),
  },
  { id: 'device', title: t('dashboard.deviceNetwork'), node: <DeviceCard status={ctx.status} /> },
  ...setupCard(ctx),
];

// Spec 026: the roof's setup stages, on the dashboard until every one is done.
const setupCard = (ctx: CardContext): MasonryItem[] => {
  const stages = setupStages(ctx.config);
  if (stages.length === 0 || setupComplete(stages)) return [];
  const node = (
    <div data-inventory="setup-card">
      <Card
        title={t('stage.title')}
        icon="roof"
        hint={t('stage.hint')}
        actions={<Pill tone="pill-amber">{stagesDone(stages)}</Pill>}
      >
        <SetupStages stages={stages} />
      </Card>
    </div>
  );
  return [{ id: 'setup', title: t('stage.title'), node }];
};
