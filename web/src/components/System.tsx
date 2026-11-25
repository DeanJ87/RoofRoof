import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { route } from 'preact-router';
import { getTimezoneFriendlyName } from '../utils/timezone';
import { useWebSocket } from '../hooks/useWebSocket';
import { useDiagLive } from '../hooks/useDiagLive';
import LiveWaiting from './LiveWaiting';
import type { SetupConfig, SystemStatus } from '../types';
import { Button, Card, Pill, ProgressMeter, ReadingRow } from './ui';
import { restartWithToast } from './toast';
import { t } from '../i18n';
import { formatAgeMs, formatBytes, formatCount, formatNumber, formatUptime } from '../i18n/format';
import { fetchSetup } from '../lib/setupApi';
import { fetchDiag } from '../lib/diagApi';
import type { DiagDocument, DiagLive } from '../types/diag';
import { boardLabel, hasEthernet, presetLabel } from '../lib/pinsView';

const formatShortAgeMs = (value: number | null | undefined): string => {
  if (typeof value !== 'number' || !Number.isFinite(value) || value <= 0 || value > 86400000) return '--';
  return formatAgeMs(value);
};

const IPV6_SCOPE_KEYS = { 'link-local': 'system.ipv6LinkLocal', 'unique-local': 'system.ipv6Local', global: 'system.ipv6Global' } as const;

const InfoRow: FunctionalComponent<{ label: string; value: string; tone?: string }> = ({ label, value, tone = '' }) => (
  <ReadingRow label={label} value={value} valueClass={tone} />
);

type Status = SystemStatus;

// A refusal (the roof busy) or no answer says so; only an accepted restart says "Restarting".
const restartDevice = () => restartWithToast();

const usedPercent = (used: number, total: number) => (total > 0 ? (used / total) * 100 : 0);

const FirmwareCard: FunctionalComponent<{ firmware: NonNullable<Status['firmware']> }> = ({ firmware }) => (
  <Card title={t('dashboard.firmware')} icon="cpu" tone="cyan">
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
        <InfoRow label={t('dashboard.uptime')} value={formatUptime(status.uptime)} />
        <InfoRow label={t('system.cpuFrequency')} value={t('system.cpufreqmhzMhz', { cpuFreqMHz: status.cpuFreqMHz })} />
        <div class="system-metric">
          <InfoRow label={t('system.freeHeap')} value={`${formatBytes(status.freeHeap)} / ${formatBytes(status.heapSize)}`} />
          <ProgressMeter value={100 - heapUsedPercent} label={t('diagnostics.freeHeap')} />
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
          <p class="subtext">{getTimezoneFriendlyName(status.time.timezone)}</p>
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
      <p class="subtext">{t('system.ntpIsDisabled')}</p>
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
      <p class="subtext">{t('system.mqttIsDisabled')}</p>
    )}
  </Card>
);

// WiFi not connected is a fault on a board without Ethernet; on one running Ethernet (spec 023) it
// is normal and reads neutral: off in Ethernet-only mode, else just not connected.
const wifiState = (wifi: Status['wifi'], ethernet: Status['ethernet']) => {
  if (wifi.connected) return { text: t('system.connected'), tone: 'tone-green' };
  if (!ethernet?.present) return { text: t('system.disconnected'), tone: 'tone-red' };
  return { text: ethernet.mode === 'ethernet' ? t('glance.wifiOffEthernet') : t('glance.wifiNotConnected'), tone: '' };
};

const WifiCard: FunctionalComponent<{ wifi: Status['wifi']; ethernet: Status['ethernet'] }> = ({ wifi, ethernet }) => (
  <Card title="WiFi" icon="wifi" tone="cyan">
    <div class="system-list">
      <InfoRow label={t('system.status')} value={wifiState(wifi, ethernet).text} tone={wifiState(wifi, ethernet).tone} />
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

// Spec 023: Ethernet on a board that has it (the WiFi card stays). In WiFi-only mode it isn't
// started: off, not a missing link.
const EthernetCard: FunctionalComponent<{ ethernet: Status['ethernet'] }> = ({ ethernet }) => (
  <Card title={t('system.ethernet')} icon="link" tone="cyan">
    <div class="system-list">
      {ethernet?.present ? (
        <>
          <InfoRow
            label={t('system.status')}
            value={ethernet.link ? t('system.connected') : t('glance.noLink')}
            tone={ethernet.link ? 'tone-green' : 'tone-amber'}
          />
          {ethernet.link && (
            <InfoRow label={t('system.linkSpeed')} value={t('settings.network.speedValue', { speed: ethernet.speedMbps })} />
          )}
          <InfoRow label={t('system.ipAddress')} value={ethernet.ip || '--'} tone="tone-cyan" />
          {ethernet.ipv6.map((address) => (
            <InfoRow key={address} label="IPv6" value={address} tone="tone-cyan" />
          ))}
          <InfoRow label={t('system.macAddress')} value={ethernet.mac || '--'} />
        </>
      ) : (
        <InfoRow label={t('system.status')} value={t('settings.network.offWifiOnly')} />
      )}
    </div>
  </Card>
);

// Spec 023: the board and pins in effect, and the way to the Pins page.
const PinsCard: FunctionalComponent<{ pins?: Status['pins'] }> = ({ pins }) => (
  <Card title={t('pins.title')} icon="cpu" tone="cyan" hint={t('system.pinsHint')}>
    {pins && (
      <div class="system-list">
        <InfoRow label={t('pins.presetLabel')} value={presetLabel(pins.preset)} />
        <InfoRow label={t('pins.board')} value={boardLabel(pins.board)} />
        {!pins.valid && pins.preset !== 'unset' && <InfoRow label={t('system.status')} value={t('pins.status.invalid')} tone="tone-red" />}
      </div>
    )}
    <div class="btn-row">
      <Button onClick={() => route('/pins')}>{t('pins.open')}</Button>
    </div>
  </Card>
);

// Spec 015 FR-006: run the setup wizard again, whatever its state.
const SETUP_STATE = { new: 'system.setupNew', offered: 'system.setupOffered', done: 'system.setupDone' } as const;

const SetupCard: FunctionalComponent = () => {
  const [setup, setSetup] = useState<SetupConfig | null>(null);
  useEffect(() => {
    void fetchSetup().then(setSetup);
  }, []);
  return (
    <Card title={t('system.setup')} icon="roof" tone="cyan" hint={t('system.setupHint')}>
      {setup && <InfoRow label={t('system.status')} value={t(SETUP_STATE[setup.state])} />}
      <div class="btn-row">
        <Button onClick={() => route('/wizard?step=1')}>{t('setup.run')}</Button>
      </div>
    </Card>
  );
};

// Spec 017 FR-031: a few live values while advanced diagnostics is on; no card
// (and no stream) while it is off. Spec 028: the page-load document only until
// the stream has connected; after a drop, no values until the next reading.
const DiagLiveCard: FunctionalComponent<{ initial: DiagLive }> = ({ initial }) => {
  const live = useDiagLive(initial);
  if (!live) return null;
  return (
    <Card title={t('diagnostics.title')} icon="cpu" tone="violet" hint={t('system.diagnosticsHint')}>
      <div class="system-list">
        <InfoRow label={t('system.diagPhase')} value={t(`diagnostics.phaseName.${live.roof.phase}`)} />
        <InfoRow label={t('diagnostics.supply')} value={`${formatNumber(live.roof.supplyVolts, 2)} V`} />
        <InfoRow label={t('diagnostics.current')} value={`${formatNumber(live.roof.motorAmps, 2)} A`} />
        <InfoRow label={t('diagnostics.stepTime')} value={`${formatCount(live.control.stepUs)} µs`} />
      </div>
      <div class="btn-row">
        <Button onClick={() => route('/diagnostics')}>{t('settings.device.openDiagnostics')}</Button>
      </div>
    </Card>
  );
};

const DiagCard: FunctionalComponent = () => {
  const [doc, setDoc] = useState<DiagDocument | null>(null);
  useEffect(() => {
    void fetchDiag().then(setDoc);
  }, []);
  return doc?.enabled ? <DiagLiveCard initial={doc} /> : null;
};

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
            {t('settings.alerts.cancel')}
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
  const { data: status, reconnecting } = useWebSocket<SystemStatus>('/ws/status');
  const [confirmRestart, setConfirmRestart] = useState(false);

  // Spec 028: no reading from this connection, no values.
  if (!status) return <LiveWaiting reconnecting={reconnecting} />;

  return (
    <div class="panel-page system-page page-enter">
      {status.firmware && <FirmwareCard firmware={status.firmware} />}
      <RuntimeCard status={status} />
      {status.partitions && <PartitionsCard partitions={status.partitions} />}
      {status.ntp && <NtpCard ntp={status.ntp} />}
      {status.mqtt && <MqttCard mqtt={status.mqtt} />}
      {hasEthernet(status) && <EthernetCard ethernet={status.ethernet} />}
      <WifiCard wifi={status.wifi} ethernet={status.ethernet} />
      <PinsCard pins={status.pins} />
      <SetupCard />
      <DiagCard />
      <ActionsCard confirming={confirmRestart} setConfirming={setConfirmRestart} />
    </div>
  );
};

export default System;
