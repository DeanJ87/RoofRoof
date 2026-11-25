import { FunctionalComponent } from 'preact';
import type { SystemStatus } from '../types';
import { Card, InfoTip, Pill } from '../components/ui';
import { formatUptime } from '../i18n/format';
import { t, type MessageKey } from '../i18n';

// Device & Network (SQMeter specs 010, 025 FR-015, FR-018, 026 FR-005): WiFi
// and uptime as tiles; every address, the .local name, MQTT and the firmware
// as one row each. Updates are by file (spec 001): there is no release check.

const rssiTone = (rssi?: number) => {
  if (typeof rssi !== 'number') return { tone: 'pill-dim', label: t('dashboard.unknown') };
  if (rssi > -60) return { tone: 'pill-green', label: t('dashboard.strong') };
  if (rssi > -75) return { tone: 'pill-cyan', label: t('dashboard.good') };
  if (rssi > -85) return { tone: 'pill-amber', label: t('dashboard.weak') };
  return { tone: 'pill-red', label: t('dashboard.poor') };
};

// PubSubClient's state codes, in the device's own words (device.api.*).
const MQTT_STATE: Record<number, MessageKey> = {
  [-4]: 'device.api.connectionTimeout',
  [-3]: 'device.api.connectionLost',
  [-2]: 'device.api.connectFailed',
  1: 'device.api.badProtocol',
  2: 'device.api.badClientId',
  3: 'device.api.unavailable',
  4: 'device.api.badCredentialsCheckUsernamePassword',
  5: 'device.api.unauthorized',
};

export const mqttStateText = (mqtt: NonNullable<SystemStatus['mqtt']>): { text: string; tone: string; reason?: string } => {
  if (mqtt.connected) return { text: t('dashboard.connected'), tone: 'pill-green' };
  const reason = MQTT_STATE[mqtt.state];
  return reason
    ? { text: t('glance.mqttCantConnect'), tone: 'pill-red', reason: t(reason) }
    : { text: t('glance.mqttRetrying'), tone: 'pill-amber' };
};

// One labelled value per row (SQMeter spec 026 DS-05): long values truncate, the
// full value is in the tooltip.
const Row: FunctionalComponent<{ label: string; hint?: string; inventory?: string; value?: string; mono?: boolean }> = ({
  label,
  hint,
  inventory,
  value,
  mono,
  children,
}) => (
  <div class="reading-row" data-inventory={inventory}>
    <span class="reading-label">
      {label} {hint && <InfoTip text={hint} />}
    </span>
    {value !== undefined ? (
      <strong class={`reading-value${mono ? ' mono ltr address-value' : ''}`} title={value}>
        {value}
      </strong>
    ) : (
      children
    )}
  </div>
);

const SCOPE_ORDER = { global: 0, 'unique-local': 1, 'link-local': 2 } as const;
const SCOPE: Record<keyof typeof SCOPE_ORDER, { label: () => string; hint?: () => string }> = {
  global: { label: () => t('glance.ipv6') },
  'unique-local': { label: () => t('glance.ipv6Local'), hint: () => t('glance.ipv6LocalHint') },
  'link-local': { label: () => t('glance.ipv6Link'), hint: () => t('glance.ipv6LinkHint') },
};

const Ipv6Rows: FunctionalComponent<{ ipv6: NonNullable<SystemStatus['wifi']['ipv6']> }> = ({ ipv6 }) => {
  const addresses = [...ipv6.addresses].sort((a, b) => SCOPE_ORDER[a.scope] - SCOPE_ORDER[b.scope]);
  if (!addresses.length)
    return (
      <Row label={t('glance.ipv6')} inventory="ipv6">
        <span class="reading-value subtext">{t('glance.ipv6None')}</span>
      </Row>
    );
  return (
    <div data-inventory="ipv6">
      {addresses.map((entry) => (
        <Row key={entry.address} label={SCOPE[entry.scope].label()} hint={SCOPE[entry.scope].hint?.()} value={entry.address} mono />
      ))}
    </div>
  );
};

type Wifi = SystemStatus['wifi'] | undefined;
type Ethernet = SystemStatus['ethernet'];

// WiFi that isn't connected has no signal to show. On a board running Ethernet (spec 023) that
// is normal, so it reads neutral: off in Ethernet-only mode, else just not connected.
const wifiPill = (wifi: Wifi, ethernet: Ethernet) => {
  if (!wifi || wifi.connected) return rssiTone(wifi?.rssi);
  if (!ethernet?.present) return { tone: 'pill-amber', label: t('glance.wifiNotConnected') };
  return { tone: 'pill-dim', label: ethernet.mode === 'ethernet' ? t('glance.wifiOffEthernet') : t('glance.wifiNotConnected') };
};

const WifiTiles: FunctionalComponent<{ wifi: Wifi; ethernet: Ethernet; uptime?: number }> = ({ wifi, ethernet, uptime }) => {
  const pill = wifiPill(wifi, ethernet);
  const connected = !wifi || wifi.connected;
  return (
    <div class="tile-grid two">
      <div class="metric-tile left">
        <div class="metric-label">{t('dashboard.wifi')}</div>
        <Pill tone={pill.tone}>{pill.label}</Pill>
        {connected && <div class="metric-sub mono">{wifi?.rssi ?? '--'} dBm</div>}
        {(connected || wifi?.ssid) && <div class="metric-sub">{wifi?.ssid || '--'}</div>}
      </div>
      <div class="metric-tile left">
        <div class="metric-label">{t('dashboard.uptime')}</div>
        <div class="metric-value small">{formatUptime(uptime)}</div>
      </div>
    </div>
  );
};

const LocalName: FunctionalComponent<{ wifi: Wifi }> = ({ wifi }) =>
  wifi?.mdns && wifi.hostname ? <Row label={t('glance.localName')} inventory="mdns" value={`${wifi.hostname}.local`} mono /> : null;

const MqttRow: FunctionalComponent<{ mqtt: SystemStatus['mqtt'] }> = ({ mqtt }) => {
  if (!mqtt?.enabled) return null;
  const state = mqttStateText(mqtt);
  return (
    <Row label={t('glance.mqtt')} hint={state.reason} inventory="mqtt-state">
      <Pill tone={state.tone}>{state.text}</Pill>
    </Row>
  );
};

const FirmwareRow: FunctionalComponent<{ version?: string }> = ({ version }) =>
  version ? (
    <Row label={t('dashboard.firmware')}>
      <Pill>v{version}</Pill>
    </Row>
  ) : null;

// Spec 023: on a board running Ethernet, its link and address (the WiFi rows stay while WiFi is connected).
const EthernetRows: FunctionalComponent<{ ethernet: SystemStatus['ethernet'] }> = ({ ethernet }) =>
  ethernet?.present ? (
    <div data-inventory="ethernet">
      <Row label={t('glance.ethernet')}>
        <Pill tone={ethernet.link ? 'pill-green' : 'pill-amber'}>{ethernet.link ? t('dashboard.connected') : t('glance.noLink')}</Pill>
      </Row>
      <Row label={t('glance.ethernetIp')} value={ethernet.ip || '--'} mono />
    </div>
  ) : null;

// WiFi's own IPv4 address, only while WiFi is connected (an unconnected WiFi has none, not 0.0.0.0).
// Beside Ethernet's address it says whose it is.
const WifiIpRow: FunctionalComponent<{ wifi: Wifi; ethernet: Ethernet }> = ({ wifi, ethernet }) =>
  wifi && !wifi.connected ? null : <Row label={ethernet?.present ? t('glance.wifiIp') : t('glance.ipv4')} value={wifi?.ip ?? '--'} mono />;

const DeviceCard: FunctionalComponent<{ status: SystemStatus | null }> = ({ status }) => (
  <Card title={t('dashboard.deviceNetwork')} icon="wifi" tone="cyan">
    <WifiTiles wifi={status?.wifi} ethernet={status?.ethernet} uptime={status?.uptime} />
    <div class="device-rows">
      <EthernetRows ethernet={status?.ethernet} />
      <WifiIpRow wifi={status?.wifi} ethernet={status?.ethernet} />
      {status?.wifi?.ipv6?.enabled && <Ipv6Rows ipv6={status.wifi.ipv6} />}
      <LocalName wifi={status?.wifi} />
      <MqttRow mqtt={status?.mqtt} />
      <FirmwareRow version={status?.firmware?.version} />
    </div>
  </Card>
);

export default DeviceCard;
