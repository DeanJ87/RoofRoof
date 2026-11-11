import { FunctionalComponent } from 'preact';
import type { DiagLive } from '../../types/diag';
import { Card, MetricTile, Note, Pill, ReadingRow } from '../ui';
import { t, tMaybe } from '../../i18n';
import { formatBytes, formatCount, formatNumber, formatUptime } from '../../i18n/format';

// The Diagnostics page's live tiles (spec 017 FR-032), one card per group:
// the roof, the motor and supply, the move's timing, the control task, the
// device and its memory. A value the device can't read shows a dash.

const DASH = '--';
const num = (value: number | null | undefined, digits: number) => formatNumber(value, digits);
export const seconds = (ms: number | null | undefined) => (typeof ms === 'number' ? formatNumber(ms / 1000, 1) : DASH);
export const roofState = (state: string) => tMaybe(`roof.state.${state}`) ?? state;

const Tiles: FunctionalComponent = ({ children }) => <div class="tile-grid diag-tiles">{children}</div>;

export const RoofCard: FunctionalComponent<{ live: DiagLive }> = ({ live }) => {
  const roof = live.roof;
  return (
    <Card
      title={t('diagnostics.roof')}
      icon="roof"
      hint={t('diagnostics.roofHint')}
      actions={<Pill tone="pill-dim">{roofState(roof.state)}</Pill>}
    >
      <Tiles>
        <MetricTile label={t('diagnostics.phase')} value={t(`diagnostics.phaseName.${roof.phase}`)} />
        <MetricTile label={t('diagnostics.direction')} value={t(`diagnostics.directionName.${roof.direction}`)} />
        <MetricTile label={t('diagnostics.duty')} value={formatCount(roof.dutyPercent)} unit="%" />
        <MetricTile label={t('diagnostics.enable')} value={roof.en ? t('diagnostics.on') : t('diagnostics.off')} />
        <MetricTile label="RPWM" value={formatCount(roof.rpwm)} />
        <MetricTile label="LPWM" value={formatCount(roof.lpwm)} />
        <MetricTile
          label={t('diagnostics.hall')}
          value={formatCount(roof.hallCounts)}
          alt={tMaybe(`roof.hall.level.${roof.hall}`) ?? roof.hall}
        />
        <MetricTile
          label={t('diagnostics.position')}
          value={num(roof.positionMm, 0)}
          unit="mm"
          alt={roof.positionKnown ? undefined : t('diagnostics.positionUnknown')}
        />
      </Tiles>
    </Card>
  );
};

export const SupplyCard: FunctionalComponent<{ live: DiagLive }> = ({ live }) => {
  const roof = live.roof;
  const noSense = roof.currentSense ? undefined : t('diagnostics.notFitted');
  return (
    <Card title={t('diagnostics.motorAndSupply')} icon="therm" tone="amber" hint={t('diagnostics.motorAndSupplyHint')}>
      <Tiles>
        <MetricTile
          label={t('diagnostics.supply')}
          value={num(roof.supplyVolts, 2)}
          unit="V"
          alt={roof.supplyVolts === null ? t('diagnostics.unreadable') : undefined}
        />
        <MetricTile label={t('diagnostics.restSupply')} value={num(roof.restVolts, 2)} unit="V" />
        <MetricTile label={t('diagnostics.sag')} value={num(roof.sagVolts, 2)} unit="V" />
        <MetricTile label={t('diagnostics.current')} value={num(roof.motorAmps, 2)} unit="A" alt={noSense} />
        <MetricTile label={t('diagnostics.power')} value={num(roof.powerWatts, 1)} unit="W" alt={noSense} />
      </Tiles>
    </Card>
  );
};

export const TimingCard: FunctionalComponent<{ live: DiagLive }> = ({ live }) => {
  const roof = live.roof;
  return (
    <Card title={t('diagnostics.moveTiming')} icon="gps" tone="green" hint={t('diagnostics.moveTimingHint')}>
      <Tiles>
        <MetricTile label={t('diagnostics.elapsed')} value={seconds(roof.elapsedMs)} unit="s" />
        <MetricTile label={t('diagnostics.remaining')} value={seconds(roof.remainingMs)} unit="s" />
        <MetricTile label={t('diagnostics.timeout')} value={seconds(roof.timeoutMs)} unit="s" />
        <MetricTile label={t('diagnostics.driveBudgetUsed')} value={seconds(roof.driveBudgetUsedMs)} unit="s" />
        <MetricTile label={t('diagnostics.driveBudget')} value={seconds(roof.driveBudgetMs)} unit="s" />
      </Tiles>
    </Card>
  );
};

// The values only the hardware measures are null in the demo and on the bench.
const SimulatedNote: FunctionalComponent<{ live: DiagLive }> = ({ live }) =>
  live.simulation === 'off' ? null : <Note>{t('diagnostics.simulatedNote')}</Note>;

export const ControlCard: FunctionalComponent<{ live: DiagLive }> = ({ live }) => {
  const c = live.control;
  return (
    <Card title={t('diagnostics.controlTask')} icon="cpu" tone="violet" hint={t('diagnostics.controlTaskHint', { period: c.periodMs })}>
      <Tiles>
        <MetricTile label={t('diagnostics.stepTime')} value={formatCount(c.stepUs)} unit="µs" />
        <MetricTile label={t('diagnostics.maxStep')} value={formatCount(c.maxStepUs)} unit="µs" />
        <MetricTile label={t('diagnostics.maxGap')} value={formatCount(c.maxGapMs)} unit="ms" />
        <MetricTile label={t('diagnostics.watchdogMargin')} value={formatCount(c.watchdogMarginMs)} unit="ms" />
        <MetricTile
          label={t('diagnostics.watchdogTrips')}
          value={formatCount(c.watchdogTrips)}
          tone={c.watchdogTrips > 0 ? 'tone-red' : ''}
        />
      </Tiles>
      <SimulatedNote live={live} />
    </Card>
  );
};

export const DeviceCard: FunctionalComponent<{ live: DiagLive }> = ({ live }) => (
  <Card title={t('diagnostics.device')} icon="wifi" hint={t('diagnostics.deviceHint')}>
    <Tiles>
      <MetricTile label={t('diagnostics.loopPass')} value={formatCount(live.loop.passMs)} unit="ms" />
      <MetricTile label={t('diagnostics.maxLoopPass')} value={formatCount(live.loop.maxPassMs)} unit="ms" />
      <MetricTile label={t('diagnostics.signal')} value={formatCount(live.wifi.rssi)} unit="dBm" />
      <MetricTile label={t('diagnostics.uptime')} value={formatUptime(live.uptimeS)} />
    </Tiles>
    <ReadingRow label={t('diagnostics.resetReason')} value={live.resetReason ?? DASH} />
    <SimulatedNote live={live} />
  </Card>
);

export const MemoryCard: FunctionalComponent<{ live: DiagLive }> = ({ live }) => {
  const m = live.memory;
  const bytes = (value: number | null) => (value === null ? DASH : formatBytes(value));
  return (
    <Card title={t('diagnostics.memory')} icon="cpu" hint={t('diagnostics.memoryHint')}>
      <ReadingRow label={t('diagnostics.freeHeap')} value={bytes(m.freeHeap)} />
      <ReadingRow label={t('diagnostics.minFreeHeap')} value={bytes(m.minFreeHeap)} />
      <ReadingRow label={t('diagnostics.largestBlock')} value={bytes(m.largestBlock)} />
      <ReadingRow label={t('diagnostics.stackControl')} value={bytes(m.stackFree.control)} />
      <ReadingRow label={t('diagnostics.stackLoop')} value={bytes(m.stackFree.loop)} />
      <ReadingRow label={t('diagnostics.stackNetwork')} value={bytes(m.stackFree.network)} />
      <SimulatedNote live={live} />
    </Card>
  );
};
