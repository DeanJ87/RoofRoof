import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { useWebSocket } from '../hooks/useWebSocket';
import { useQuiet } from '../hooks/useQuiet';
import type { Config, SystemStatus } from '../types';
import { Button } from './ui';
import { getJson } from '../lib/api';
import Masonry, { MasonryItem, mergeOrder, moveInOrder } from './Masonry';
import { t } from '../i18n';
import StatusCard from '../dashboard/StatusCard';
import { glanceItems } from '../dashboard/glance';
import { useEffectiveReport } from '../dashboard/useGlanceData';
import { useAlertSchedule } from '../hooks/useAlertSchedule';
import { StatusDot } from './dashboard/MiniSpark';
import { dashboardCards } from './dashboard/cards';
import { DEFAULT_ORDER, loadOrder, saveOrder } from './dashboard/order';

// Nothing received yet; once data has arrived, a lost connection keeps the
// last values on screen, greyed, with the Status card saying so.
const Waiting: FunctionalComponent<{ connected: boolean }> = ({ connected }) => (
  <div class="empty-state">
    <StatusDot ok={false} />
    <h2>{connected ? t('dashboard.waitingForTheDevice') : t('dashboard.connecting')}</h2>
    <p>{t('dashboard.openingTheLiveWebsocketStream')}</p>
  </div>
);

const Toolbar: FunctionalComponent<{ arranging: boolean; setArranging: (on: boolean) => void; reset: () => void }> = ({
  arranging,
  setArranging,
  reset,
}) => (
  <div class="dashboard-toolbar">
    {arranging && (
      <Button small variant="ghost" onClick={reset}>
        {t('dashboard.resetOrder')}
      </Button>
    )}
    <Button small variant={arranging ? 'primary' : 'ghost'} onClick={() => setArranging(!arranging)}>
      {arranging ? t('dashboard.done') : t('dashboard.arrange')}
    </Button>
  </div>
);

const Dashboard: FunctionalComponent = () => {
  const [savedOrder, setSavedOrder] = useState<string[]>(loadOrder);
  const [arranging, setArranging] = useState(false);
  const { data: status, connected, lastMessageAt } = useWebSocket<SystemStatus>('/ws/status');
  const quiet = useQuiet(lastMessageAt);
  const [config, setConfig] = useState<Config | null>(null);
  const configRevision = status?.configRevision;
  const effective = useEffectiveReport(configRevision);
  const { schedule, pauseOrResume } = useAlertSchedule(status?.alerts);

  // Read again whenever the device's settings change. A failed read keeps
  // what's shown.
  useEffect(() => {
    getJson<Config>('/api/config')
      .then((data) => data && setConfig(data))
      .catch(() => undefined);
  }, [configRevision]);

  const items = glanceItems({ status, config, effective, connected, quiet, schedule });
  const onAction = () => void pauseOrResume(true);

  if (!status) return <Waiting connected={connected} />;

  const statusCard: MasonryItem = { id: 'status', title: t('status.title'), node: <StatusCard items={items} onAction={onAction} /> };
  const visible = [statusCard, ...dashboardCards({ status })];
  const fullOrder = mergeOrder(savedOrder, DEFAULT_ORDER);
  const ordered = fullOrder.map((id) => visible.find((card) => card.id === id)).filter((card): card is MasonryItem => Boolean(card));

  const move = (id: string, toIndex: number) => {
    const next = moveInOrder(
      fullOrder,
      ordered.map((card) => card.id),
      id,
      toIndex,
    );
    setSavedOrder(next);
    saveOrder(next);
  };
  const reset = () => {
    setSavedOrder([]);
    saveOrder([]);
  };

  return (
    <div class={`dashboard page-enter${connected ? '' : ' is-disconnected'}`}>
      <Toolbar arranging={arranging} setArranging={setArranging} reset={reset} />
      <Masonry items={ordered} editing={arranging} onMove={move} />
    </div>
  );
};

export default Dashboard;
