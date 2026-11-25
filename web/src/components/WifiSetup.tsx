import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import type { EthernetStatus, WiFiNetwork } from '../types';
import { useWifiScan } from '../hooks/useWifiScan';
import { useWifiConnect, type WifiConnectPhase } from '../hooks/useWifiConnect';
import { useNetworkMode } from '../hooks/usePins';
import { Button, Card, Note } from './ui';
import { Field, TextInput } from './settings/controls';
import { t } from '../i18n';
import { tRich } from '../i18n/rich';

// Where the "RoofRoof-Setup" hotspot's captive portal lands: pick a network, enter
// its password, connect. The device saves it and restarts onto that network.

const POLL_MS = 2000;
const OTHER = '\u0000other';

const ConnectedCard: FunctionalComponent<{ phase: Extract<WifiConnectPhase, { kind: 'connected' }> }> = ({ phase }) => {
  const local = phase.mdns !== false && phase.hostname ? `http://${phase.hostname}.local` : null;
  return (
    <div class="panel-page compact-page page-enter">
      <Card title={t('wifiSetup.connected')} icon="wifi" tone="green">
        <div class="card-body">
          <p>{tRich('wifiSetup.joinedReconnect', { ssid: <strong>{phase.ssid}</strong> })}</p>
          <ul class="wifi-addresses">
            {local && (
              <li>
                <a href={local}>{local}</a>
              </li>
            )}
            <li>
              <a href={`http://${phase.ip}`}>{`http://${phase.ip}`}</a>
            </li>
          </ul>
          <Note>{t('wifiSetup.theRoofroofSetupHotspotTurns')}</Note>
        </div>
      </Card>
    </div>
  );
};

const NetworkOptions: FunctionalComponent<{
  networks: WiFiNetwork[];
  selected: string | null;
  onSelect: (ssid: string) => void;
  disabled: boolean;
}> = ({ networks, selected, onSelect, disabled }) => (
  <div class="wifi-list" role="radiogroup" aria-label={t('wifiSetup.wifiNetworks')}>
    {networks.map((n) => (
      <button
        key={n.ssid}
        type="button"
        role="radio"
        aria-checked={selected === n.ssid}
        class={`wifi-option${selected === n.ssid ? ' is-selected' : ''}`}
        onClick={() => onSelect(n.ssid)}
        disabled={disabled}
      >
        <span>{n.ssid}</span>
        <span class="muted">
          {n.encryption === 'secured' ? '🔒 ' : ''}
          {n.rssi} dBm
        </span>
      </button>
    ))}
    <button
      type="button"
      role="radio"
      aria-checked={selected === OTHER}
      class={`wifi-option${selected === OTHER ? ' is-selected' : ''}`}
      onClick={() => onSelect(OTHER)}
      disabled={disabled}
    >
      <span>{t('wifiSetup.otherNetwork')}</span>
    </button>
  </div>
);

const WIFI_OFF_ID = 'wifi-setup-off';

// Spec 023: on a board with Ethernet, WiFi is optional; in Ethernet-only mode (in effect since
// boot) it is off, and Scan and Connect say so.
const EthernetNote: FunctionalComponent<{ ethernet: EthernetStatus | null }> = ({ ethernet }) => {
  if (!ethernet?.present) return null;
  if (ethernet.mode === 'ethernet')
    return (
      <Note tone="warn" id={WIFI_OFF_ID}>
        {t('wifiSetup.ethernetOnly')}
      </Note>
    );
  return (
    <Note tone="ok">{ethernet.link && ethernet.ip ? t('wifiSetup.onEthernet', { ip: ethernet.ip }) : t('wifiSetup.wifiOptional')}</Note>
  );
};

// The network name (for Other) and password the chosen network needs.
const CredentialFields: FunctionalComponent<{
  other: boolean;
  needsPassword: boolean;
  otherSsid: string;
  setOtherSsid: (value: string) => void;
  password: string;
  setPassword: (value: string) => void;
}> = ({ other, needsPassword, otherSsid, setOtherSsid, password, setPassword }) => (
  <div class="form-grid">
    {other && (
      <Field label={t('wifiSetup.networkName')}>
        <TextInput dataField="setup.ssid" value={otherSsid} onInput={setOtherSsid} />
      </Field>
    )}
    {needsPassword && (
      <Field label={t('wifiSetup.password')}>
        <TextInput dataField="setup.password" type="password" value={password} onInput={setPassword} />
      </Field>
    )}
  </div>
);

// WiFi is off while the network mode in effect is Ethernet only (the device refuses a scan or a join).
const useWifiOff = () => {
  const { ethernet, loaded } = useNetworkMode();
  const wifiOff = Boolean(ethernet?.present && ethernet.mode === 'ethernet');
  return { ethernet, loaded, wifiOff, offReason: wifiOff ? WIFI_OFF_ID : undefined };
};

const WifiSetup: FunctionalComponent<{ path?: string; pollMs?: number }> = ({ pollMs = POLL_MS }) => {
  const { networks, scanning, error: scanError, scan } = useWifiScan();
  const { phase, connect: join } = useWifiConnect(pollMs);
  const [selected, setSelected] = useState<string | null>(null);
  const [otherSsid, setOtherSsid] = useState('');
  const [password, setPassword] = useState('');
  const { ethernet, loaded, wifiOff, offReason } = useWifiOff();

  // The first scan once the network mode is known: none while WiFi is off (the device refuses it).
  useEffect(() => {
    if (loaded && !wifiOff) void scan();
  }, [scan, loaded, wifiOff]);

  const ssid = selected === OTHER ? otherSsid.trim() : (selected ?? '');
  const network = networks.find((n) => n.ssid === selected);
  const needsPassword = selected === OTHER || network?.encryption !== 'open';
  const connect = () => join(ssid, needsPassword ? password : '');

  if (phase.kind === 'connected') return <ConnectedCard phase={phase} />;

  const connecting = phase.kind === 'connecting';
  return (
    <div class="panel-page compact-page page-enter">
      <div>
        <h2 class="page-title">{t('wifiSetup.wifiSetup')}</h2>
        <p class="subtext">{t('wifiSetup.chooseTheNetworkRoofroofShould')}</p>
        <EthernetNote ethernet={ethernet} />
      </div>
      <Card
        title={t('wifiSetup.networks')}
        icon="wifi"
        actions={
          <Button
            small
            onClick={() => void scan()}
            busy={scanning}
            busyLabel={t('common.scanning')}
            disabled={connecting || wifiOff}
            describedBy={offReason}
          >
            {t('wifiSetup.scanAgain')}
          </Button>
        }
      >
        <div class="card-body">
          {scanError && <Note tone="bad">{scanError}</Note>}
          {!scanning && networks.length === 0 && !scanError && <Note>{t('wifiSetup.noNetworksFound')}</Note>}
          <NetworkOptions networks={networks} selected={selected} onSelect={setSelected} disabled={connecting} />

          {selected !== null && (
            <CredentialFields
              other={selected === OTHER}
              needsPassword={needsPassword}
              otherSsid={otherSsid}
              setOtherSsid={setOtherSsid}
              password={password}
              setPassword={setPassword}
            />
          )}

          {phase.kind === 'failed' && <Note tone="bad">{phase.text}</Note>}
          <div class="btn-row">
            <Button
              variant="primary"
              onClick={() => void connect()}
              disabled={!ssid || wifiOff}
              describedBy={offReason}
              busy={connecting}
              busyLabel={t('common.connecting')}
            >
              {t('wifiSetup.connect')}
            </Button>
          </div>
        </div>
      </Card>
    </div>
  );
};

export default WifiSetup;
