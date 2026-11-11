import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import { getTimezoneFriendlyName } from '../utils/timezone';
import { useWebSocket } from '../hooks/useWebSocket';
import type { SystemStatus } from '../types';
import { Button, Card, Pill, ProgressMeter, ReadingRow } from './ui';
import { showToast } from './toast';
import { t } from '../i18n';
import { formatAgeMs, formatBytes, formatUptime } from '../i18n/format';
import { post } from '../lib/api';

const formatShortAgeMs = (value: number | null | undefined): string => {
  if (typeof value !== 'number' || !Number.isFinite(value) || value <= 0 || value > 86400000) return '--';
  return formatAgeMs(value);
};

const IPV6_SCOPE_KEYS = { 'link-local': 'system.ipv6LinkLocal', 'unique-local': 'system.ipv6Local', global: 'system.ipv6Global' } as const;

const InfoRow: FunctionalComponent<{ label: string; value: string; tone?: string }> = ({ label, value, tone = '' }) => (
  <ReadingRow label={label} value={value} valueClass={tone} />
);

type Status = SystemStatus;

const restartDevice = async () => {
  try {
    await post('/api/restart');
    showToast({ message: t('common.restarting') });
  } catch {
    showToast({ message: t('system.couldNotReachTheDevice'), tone: 'bad' });
  }
};

const usedPercent = (used: number, total: number) => (total > 0 ? (used / total) * 100 : 0);

const FirmwareCard: FunctionalComponent<{ firmware: NonNullable<Status['firmware']> }> = ({ firmware }) => (
  <Card title={t('system.firmware')} icon="cpu" tone="cyan">
    <div class="system-row-grid">
      <InfoRow label={t('system.name')} value={firmware.name} />
      <InfoRow label={t('system.version')} value={`v${firmware.version}`} tone="tone-cyan" />
      <InfoRow label={t('system.build')} value={`${firmware.buildDate} ${firmware.buildTime}`} />
    </div>
  </Card>
);

const RuntimeCard: FunctionalComponent<{ status: Status }> = ({ status }) => {
  const heapUsedPercent = usedPercent(status.heapSize - status.freeHeap, status.heapSize);
  const flashUsedPercent = usedPercent(status.sketchSize, status.flashSize);
  const fsUsedPercent = usedPercent(status.fsUsed, status.fsTotal);
  return (
    <Card title={t('system.runtimeStatus')} icon="cpu" tone="violet">
      <div class="system-row-grid">
        <InfoRow label={t('system.uptime')} value={formatUptime(status.uptime)} />
        <InfoRow label={t('system.cpuFrequency')} value={t('system.cpufreqmhzMhz', { cpuFreqMHz: status.cpuFreqMHz })} />
        <div class="system-metric">
          <InfoRow label={t('system.freeHeap')} value={`${formatBytes(status.freeHeap)} / ${formatBytes(status.heapSize)}`} />
          <ProgressMeter value={100 - heapUsedPercent} label={t('system.freeHeap2')} />
        </div>
        <div class="system-metric">
          <InfoRow label={t('system.flash')} value={`${formatBytes(status.sketchSize)} / ${formatBytes(status.flashSize)}`} />
          <ProgressMeter value={flashUsedPercent} label={t('system.flashUsed')} />
        </div>
        <div class="system-metric">
          <InfoRow label={t('system.filesystem')} value={`${formatBytes(status.fsUsed)} / ${formatBytes(status.fsTotal)}`} />
          <ProgressMeter value={fsUsedPercent} label={t('system.filesystemUsed')} />
        </div>
        <div>
          <div class="reading-row">
            <span class="reading-label">{t('system.currentTime')}</span>
            {status.ntp?.synced && <Pill tone="pill-green">NTP</Pill>}
          </div>
          <strong class="system-time">{status.time.iso}</strong>
          <p class="system-subtle">{getTimezoneFriendlyName(status.time.timezone)}</p>
        </div>
      </div>
    </Card>
  );
};

const PartitionsCard: FunctionalComponent<{ partitions: NonNullable<Status['partitions']> }> = ({ partitions }) => (
  <Card title={t('system.flashPartitions')} icon="upload" tone="cyan">
    <div class="system-row-grid">
      <InfoRow label={t('system.currentSlot')} value={partitions.runningSlot} />
      <InfoRow label={t('system.nextUpdateSlot')} value={partitions.nextSlot} />
      <InfoRow label={t('system.otaSlotSize')} value={formatBytes(partitions.runningSize)} />
      {partitions.nvs && (
        <>
          <InfoRow label={t('system.nvsUsed')} value={String(partitions.nvs.usedEntries)} />
          <InfoRow label={t('system.nvsFree')} value={String(partitions.nvs.freeEntries)} />
          <InfoRow label={t('system.namespaces')} value={String(partitions.nvs.namespaceCount)} />
        </>
      )}
    </div>
  </Card>
);

type Ntp = NonNullable<Status['ntp']>;

const ntpStatusText = (ntp: Ntp) => (ntp.synced ? t('system.synced') : ntp.status === 1 ? t('system.syncing') : t('system.notSynced'));

const NtpCard: FunctionalComponent<{ ntp: Ntp }> = ({ ntp }) => (
  <Card title={t('system.networkTimeNtp')} icon="gps" tone="green">
    {ntp.enabled ? (
      <div class="system-list">
        <InfoRow label={t('system.status')} value={ntpStatusText(ntp)} tone={ntp.synced ? 'tone-green' : 'tone-amber'} />
        <InfoRow label={t('system.server')} value={ntp.server} />
        <InfoRow label={t('system.lastSyncAge')} value={formatShortAgeMs(ntp.lastSync)} />
        <InfoRow label={t('system.nextSync')} value={formatShortAgeMs(ntp.nextSync)} />
        <InfoRow label={t('system.clockDrift')} value={`${ntp.drift}s`} />
      </div>
    ) : (
      <p class="system-subtle">{t('system.ntpIsDisabled')}</p>
    )}
  </Card>
);

const MqttCard: FunctionalComponent<{ mqtt: NonNullable<Status['mqtt']> }> = ({ mqtt }) => (
  <Card title="MQTT" icon="wifi" tone="cyan">
    {mqtt.enabled ? (
      <div class="system-list">
        <InfoRow
          label={t('system.status')}
          value={mqtt.connected ? t('system.connected') : t('system.disconnected')}
          tone={mqtt.connected ? 'tone-green' : 'tone-red'}
        />
        <InfoRow label={t('system.broker')} value={`${mqtt.broker}:${mqtt.port}`} />
        <InfoRow label={t('system.topic')} value={mqtt.topic} />
      </div>
    ) : (
      <p class="system-subtle">{t('system.mqttIsDisabled')}</p>
    )}
  </Card>
);

const WifiCard: FunctionalComponent<{ wifi: Status['wifi'] }> = ({ wifi }) => (
  <Card title="WiFi" icon="wifi" tone="cyan">
    <div class="system-list">
      <InfoRow
        label={t('system.status')}
        value={wifi.connected ? t('system.connected') : t('system.disconnected')}
        tone={wifi.connected ? 'tone-green' : 'tone-red'}
      />
      {wifi.connected && (
        <>
          <InfoRow label="SSID" value={wifi.ssid} />
          <InfoRow label={t('system.ipAddress')} value={wifi.ip} tone="tone-cyan" />
          {wifi.ipv6?.addresses.map((entry) => (
            <InfoRow
              key={entry.address}
              label={t('system.ipv6Scope', { scope: t(IPV6_SCOPE_KEYS[entry.scope]) })}
              value={entry.address}
              tone="tone-cyan"
            />
          ))}
          <InfoRow label={t('system.signal')} value={`${wifi.rssi} dBm`} />
          <InfoRow label={t('system.macAddress')} value={wifi.mac} />
        </>
      )}
    </div>
  </Card>
);

const ActionsCard: FunctionalComponent<{ confirming: boolean; setConfirming: (confirming: boolean) => void }> = ({
  confirming,
  setConfirming,
}) => (
  <Card title={t('system.actions')} icon="cpu" tone="red">
    <div class="btn-row">
      {confirming ? (
        <>
          <Button
            variant="danger"
            onClick={() => {
              setConfirming(false);
              void restartDevice();
            }}
          >
            {t('system.restartNow')}
          </Button>
          <Button variant="ghost" onClick={() => setConfirming(false)}>
            {t('system.cancel')}
          </Button>
        </>
      ) : (
        <Button variant="danger" onClick={() => setConfirming(true)}>
          {t('system.restartDevice')}
        </Button>
      )}
    </div>
  </Card>
);

const System: FunctionalComponent = () => {
  const { data: status, connected } = useWebSocket<SystemStatus>('/ws/status');
  const [confirmRestart, setConfirmRestart] = useState(false);

  if (!connected || !status) {
    return (
      <div class="empty-state">
        <h2>{t('common.loading')}</h2>
        <p>{t('system.connectingToDevice')}</p>
      </div>
    );
  }

  return (
    <div class="panel-page system-page page-enter">
      {status.firmware && <FirmwareCard firmware={status.firmware} />}
      <RuntimeCard status={status} />
      {status.partitions && <PartitionsCard partitions={status.partitions} />}
      {status.ntp && <NtpCard ntp={status.ntp} />}
      {status.mqtt && <MqttCard mqtt={status.mqtt} />}
      <WifiCard wifi={status.wifi} />
      <ActionsCard confirming={confirmRestart} setConfirming={setConfirmRestart} />
    </div>
  );
};

export default System;
