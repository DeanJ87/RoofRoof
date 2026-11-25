import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { useWebSocket } from '../hooks/useWebSocket';
import type { Config, SystemStatus } from '../types';
import { Button } from './ui';
import { getJson } from '../lib/api';
import Masonry, { MasonryItem, mergeOrder, moveInOrder } from './Masonry';
import { t } from '../i18n';
import StatusCard from '../dashboard/StatusCard';
import { glanceItems } from '../dashboard/glance';
import { useEffectiveReport } from '../dashboard/useGlanceData';
import { useAlertSchedule } from '../hooks/useAlertSchedule';
import { dashboardCards } from './dashboard/cards';
import { DEFAULT_ORDER, loadOrder, saveOrder } from './dashboard/order';
import { useReconnectedAt } from '../hooks/useRoofCommand';
import RoofCard from '../dashboard/RoofCard';
import LiveWaiting, { LiveStale } from './LiveWaiting';

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
  const { data: current, connected, lastMessageAt, reconnecting, lastKnown, tooMany } = useWebSocket<SystemStatus>('/ws/status');
  // Spec 037: after a drop the last reading stays, greyed and never live.
  const status = current ?? lastKnown ?? null;
  const reconnectedAt = useReconnectedAt(connected, lastMessageAt);
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

  const items = glanceItems({ status, config, effective, schedule });
  const onAction = () => void pauseOrResume(true);

  // Spec 028: a quiet socket is closed by useWebSocket, so the status shown is
  // never quiet; spec 037: the last reading after a drop counts as quiet (not live).
  const live = { status, connected, quiet: !current, reconnectedAt, lastMessageAt };
  // Spec 028: nothing received yet, no state: the waiting note. Stop never
  // waits for live data: the Roof card (Stop enabled, Open and Close disabled
  // with "No live data") shows under it.
  if (!status)
    return (
      <div class="dashboard page-enter">
        <LiveWaiting reconnecting={reconnecting} tooMany={tooMany} />
        <RoofCard live={live} />
      </div>
    );

  const statusCard: MasonryItem = { id: 'status', title: t('status.title'), node: <StatusCard items={items} onAction={onAction} /> };
  const visible = [statusCard, ...dashboardCards({ status, config, live })];
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

  const page = <Masonry items={ordered} editing={arranging} onMove={move} />;
  return (
    <div class="dashboard page-enter">
      <Toolbar arranging={arranging} setArranging={setArranging} reset={reset} />
      {current ? page : <LiveStale tooMany={tooMany}>{page}</LiveStale>}
    </div>
  );
};

export default Dashboard;
