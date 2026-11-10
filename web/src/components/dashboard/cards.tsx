import type { SystemStatus } from '../../types';
import type { MasonryItem } from '../Masonry';
import DeviceCard from '../../dashboard/DeviceCard';
import { t } from '../../i18n';

// What the dashboard's cards are drawn from.
export interface CardContext {
  status: SystemStatus | null;
}

// Every card there is, in no particular order (the order is the user's). The
// Status card is added by Dashboard.tsx; spec 009 adds the roof.
export const dashboardCards = (ctx: CardContext): MasonryItem[] => [
  { id: 'device', title: t('dashboard.deviceNetwork'), node: <DeviceCard status={ctx.status} /> },
];
