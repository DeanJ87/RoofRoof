import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useWifiScan } from '../../hooks/useWifiScan';
import { useMqttTest } from '../../hooks/useMqttTest';
import { defaultAlpacaConfig, defaultHomeAssistant } from './defaults';
import type { SettingsTabProps } from './context';
import { Button, Note } from '../ui';
import { EthernetCard } from './EthernetCard';
import { hasEthernet } from '../../lib/pinsView';
import {
  ActionButton,
  DepNote,
  DepToggle,
  Field,
  Group,
  NumberInput,
  ResultNote,
  SelectInput,
  SettingsCard,
  StatusBadge,
  TextInput,
  Toggle,
} from './controls';
import { FollowsSqmeter, NeedsNote } from './FollowsSqmeter';
import { locked as isLocked, mqttShown } from '../../lib/syncView';
import { t } from '../../i18n';

type NetworkTabProps = SettingsTabProps & { originalWifiSsid: string | null };

const WIFI_OFF_ID = 'wifi-off-note';

// WiFi is off while the network mode in effect (since boot) is Ethernet only: the device refuses a scan.
const wifiOff = (status: NetworkTabProps['status']) => status?.ethernet?.mode === 'ethernet';

// Spec 023: on a board with Ethernet, WiFi is optional (or off in Ethernet-only mode, as in effect).
const WifiRole: FunctionalComponent<NetworkTabProps> = ({ status }) => {
  // In WiFi only Ethernet isn't started: WiFi is the network, not an option.
  if (!hasEthernet(status) || status?.ethernet?.mode === 'wifi') return null;
  return wifiOff(status) ? (
    <Note id={WIFI_OFF_ID}>{t('settings.network.wifiOff')}</Note>
  ) : (
    <Note>{t('settings.network.wifiOptional')}</Note>
  );
};

// The WiFi network, hostname, mDNS and IPv6.
const WifiCard: FunctionalComponent<NetworkTabProps> = (props) => {
  const { config, update, updateMany, error, originalWifiSsid, deps, fix } = props;
  const { networks, scanning, error: scanError, scan } = useWifiScan();
  const selectNetwork = (ssid: string) =>
    updateMany([
      [['wifi', 'ssid'], ssid],
      // A different network needs its own password; keep the stored one only
      // for the network the device is already on.
      [['wifi', 'password'], ssid !== originalWifiSsid ? '' : config.wifi.password],
    ]);

  const ssid = config.wifi.ssid;
  const showPassword = originalWifiSsid !== null && ssid !== originalWifiSsid;
  const networkOptions = [
    ...(ssid && !networks.some((n) => n.ssid === ssid) ? [{ value: ssid, label: ssid }] : []),
    ...networks.map((n) => ({ value: n.ssid, label: `${n.ssid}  ${n.rssi} dBm${n.encryption === 'secured' ? ' 🔒' : ''}` })),
    { value: 'OTHER', label: t('common.other') },
  ];
  return (
    <SettingsCard id="wifi" title="WiFi">
      <WifiRole {...props} />
      <div class="form-grid">
        <Field label={t('settings.network.network')} error={error('wifi.ssid')}>
          <div class="input-row">
            <SelectInput
              dataField="wifi.ssid"
              value={ssid === '' ? 'OTHER' : ssid}
              options={networkOptions}
              onChange={(v) => selectNetwork(v === 'OTHER' ? '' : v)}
            />
            <ActionButton
              onClick={scan}
              busy={scanning}
              busyLabel={t('common.scanning')}
              disabled={wifiOff(props.status)}
              describedBy={wifiOff(props.status) ? WIFI_OFF_ID : undefined}
            >
              {t('settings.network.scan')}
            </ActionButton>
          </div>
          {ssid === '' && (
            <TextInput
              dataField="wifi.ssid"
              value={ssid}
              placeholder={t('settings.network.networkName')}
              onInput={(v) => selectNetwork(v)}
            />
          )}
          <ResultNote result={scanError ? { type: 'error', text: scanError } : null} />
        </Field>
        {showPassword && (
          <Field label={t('settings.network.password')}>
            <TextInput
              dataField="wifi.password"
              type="password"
              value={config.wifi.password}
              onInput={(v) => update(['wifi', 'password'], v)}
            />
          </Field>
        )}
        <Field label={t('settings.network.hostname')} error={error('wifi.hostname')}>
          <TextInput dataField="wifi.hostname" value={config.wifi.hostname} onInput={(v) => update(['wifi', 'hostname'], v)} />
        </Field>
      </div>
      <Toggle
        label={t('settings.network.reconnectAutomatically')}
        checked={config.wifi.autoReconnect}
        onChange={(v) => update(['wifi', 'autoReconnect'], v)}
      />
      <DepToggle
        entry={deps.get('wifi.mdns')}
        onFix={fix}
        label={t('settings.network.advertiseOnTheNetworkMdns')}
        hint={t('settings.network.reachableAtHttpValueLocal', { value: config.wifi.hostname || 'roofroof' })}
        checked={config.wifi.mdns ?? true}
        onChange={(v) => update(['wifi', 'mdns'], v)}
      />
      <DepToggle
        entry={deps.get('wifi.ipv6')}
        onFix={fix}
        label="IPv6"
        hint={t('settings.network.alsoReachableOverIpv6')}
        checked={config.wifi.ipv6 ?? true}
        onChange={(v) => update(['wifi', 'ipv6'], v)}
      />
    </SettingsCard>
  );
};

// Broker, credentials, base topic and interval.
// Spec 020: the broker, port and user name are read-only while the MQTT group follows SQMeter.
export const MqttFields: FunctionalComponent<SettingsTabProps> = (props) => {
  const { config, update, error, status } = props;
  const base = config.mqtt.topic || 'roofroof';
  const locked = isLocked(config, 'mqtt');
  return (
    <>
      <FollowsSqmeter group="mqtt" {...props} />
      <div class="form-grid">
        <Field label={t('settings.network.broker')} error={error('mqttBroker')}>
          <TextInput
            dataField="mqttBroker"
            disabled={locked}
            value={config.mqtt.broker}
            placeholder="192.168.1.100 or fd00::10"
            onInput={(v) => update(['mqtt', 'broker'], v)}
          />
        </Field>
        <Field label={t('settings.network.port')} error={error('mqttPort')}>
          <NumberInput
            dataField="mqttPort"
            disabled={locked}
            integer
            min={1}
            max={65535}
            value={config.mqtt.port}
            onChange={(v) => update(['mqtt', 'port'], Math.max(1, Math.min(65535, v || 1883)))}
          />
        </Field>
        <Field label={t('settings.network.username')}>
          <TextInput
            dataField="mqtt.username"
            disabled={locked}
            value={config.mqtt.username}
            placeholder={t('settings.network.optional')}
            onInput={(v) => update(['mqtt', 'username'], v)}
          />
        </Field>
        <Field label={t('settings.network.password')}>
          <TextInput
            type="password"
            dataField="mqtt.password"
            value={config.mqtt.password ?? ''}
            placeholder={t('settings.network.optional')}
            onInput={(v) => update(['mqtt', 'password'], v)}
          />
          <NeedsNote status={status} need="mqttPassword" filled={!!config.mqtt.password} cleared={config.mqtt.password === null} />
        </Field>
        <Field label={t('settings.network.baseTopic')} error={error('mqttTopic')} hint={t('settings.network.topicsGoUnderBase', { base })}>
          <TextInput dataField="mqttTopic" value={config.mqtt.topic} placeholder="roofroof" onInput={(v) => update(['mqtt', 'topic'], v)} />
        </Field>
        <Field label={t('settings.network.publishEvery')} error={error('mqttInterval')}>
          <NumberInput
            dataField="mqttInterval"
            integer
            min={1}
            max={86400}
            unit="s"
            value={config.mqtt.publishIntervalMs / 1000}
            onChange={(v) => update(['mqtt', 'publishIntervalMs'], Math.max(1, v || 60) * 1000)}
          />
        </Field>
      </div>
    </>
  );
};

const MqttTest: FunctionalComponent<{ config: SettingsTabProps['config']; test: ReturnType<typeof useMqttTest> }> = ({ config, test }) => {
  const { testingMqtt, mqttResult, testMqtt } = test;
  return (
    <div class="btn-row">
      <ActionButton
        onClick={testMqtt}
        busy={testingMqtt}
        busyLabel={t('common.testing')}
        disabled={!config.mqtt.broker || !config.mqtt.port}
      >
        {t('settings.network.testConnection')}
      </ActionButton>
      <ResultNote result={mqttResult} />
    </div>
  );
};

// Open, close and stop over MQTT (spec 008): off unless switched on.
const RoofCommandsGroup: FunctionalComponent<SettingsTabProps> = ({ config, update }) => (
  <Group title={t('settings.network.roofCommands')}>
    <Toggle
      dataField="mqttRoofCommands"
      label={t('settings.network.acceptRoofCommands')}
      checked={config.mqtt.roofCommands ?? false}
      onChange={(v) => update(['mqtt', 'roofCommands'], v)}
      hint={t('settings.network.roofCommandsHint', { base: config.mqtt.topic || 'roofroof' })}
    />
  </Group>
);

// Home Assistant MQTT discovery.
const HomeAssistantGroup: FunctionalComponent<SettingsTabProps> = ({ config, update, error, deps, fix }) => {
  const homeAssistant = { ...defaultHomeAssistant, ...config.mqtt.homeAssistant };
  return (
    <Group title="Home Assistant">
      <DepToggle
        entry={deps.get('mqtt.homeAssistant.enabled')}
        onFix={fix}
        label={t('settings.network.mqttDiscovery')}
        checked={homeAssistant.enabled}
        onChange={(v) => update(['mqtt', 'homeAssistant', 'enabled'], v)}
        hint={t('settings.network.announcesTheDeviceToHomeAssistant')}
      />
      {homeAssistant.enabled && deps.get('mqtt.homeAssistant.alertsSwitch').state === 'inactive' && (
        <div class="indent">
          <DepNote entry={deps.get('mqtt.homeAssistant.alertsSwitch')} onFix={fix} prefix={t('settings.network.noAlertsSwitch')} />
        </div>
      )}
      {homeAssistant.enabled && (
        <div class="form-grid indent">
          <Field
            label={t('settings.network.discoveryPrefix')}
            error={error('mqtt.homeAssistant.discoveryPrefix')}
            hint={t('settings.network.homeAssistantSDefaultIs')}
          >
            <TextInput
              value={homeAssistant.discoveryPrefix}
              placeholder="homeassistant"
              onInput={(v) => update(['mqtt', 'homeAssistant', 'discoveryPrefix'], v)}
            />
          </Field>
        </div>
      )}
    </Group>
  );
};

const mqttBadge = ({ config, status }: SettingsTabProps) => {
  const connected = status?.mqtt ? status.mqtt.connected : null;
  if (!config.mqtt.enabled || connected === null) return undefined;
  return (
    <StatusBadge tone={connected ? 'ok' : 'bad'} label={connected ? t('settings.network.connected') : t('settings.network.notConnected')} />
  );
};

// ASCOM Alpaca: discovery, the management API and the roof's Dome (spec 006).
const AlpacaCard: FunctionalComponent<SettingsTabProps> = ({ config, update }) => {
  const alpaca = { ...defaultAlpacaConfig, ...config.alpaca };
  return (
    <SettingsCard id="alpaca" title="ASCOM Alpaca" hint={t('settings.network.alpacaHint')}>
      <Toggle label={t('settings.network.serveAlpaca')} checked={alpaca.enabled} onChange={(v) => update(['alpaca', 'enabled'], v)} />
      <div>
        <Button variant="link" onClick={() => route('/alpaca')}>
          {t('settings.network.alpacaDetails')}
        </Button>
      </div>
    </SettingsCard>
  );
};

const NetworkTab: FunctionalComponent<NetworkTabProps> = (props) => {
  const { config, update } = props;
  const mqttTest = useMqttTest(config);
  return (
    <>
      {hasEthernet(props.status) && <EthernetCard {...props} />}
      <WifiCard {...props} />

      <SettingsCard id="mqtt" title="MQTT" hint={t('settings.network.mqttHint')} badge={mqttBadge(props)}>
        <Toggle
          label={t('settings.network.publishToABroker')}
          checked={config.mqtt.enabled}
          onChange={(v) => update(['mqtt', 'enabled'], v)}
        />
        {config.mqtt.enabled ? (
          <>
            <MqttFields {...props} />
            <MqttTest config={config} test={mqttTest} />

            <RoofCommandsGroup {...props} />
            <HomeAssistantGroup {...props} />
          </>
        ) : (
          // Spec 020: what was copied or synced from SQMeter stays visible and editable while MQTT is off.
          mqttShown(config, props.status?.sync) && <MqttFields {...props} />
        )}
      </SettingsCard>

      <AlpacaCard {...props} />
    </>
  );
};

export default NetworkTab;
