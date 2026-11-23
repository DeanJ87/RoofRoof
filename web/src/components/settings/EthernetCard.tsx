import { FunctionalComponent } from 'preact';
import type { EthernetStatus, NetworkMode } from '../../types';
import type { SettingsTabProps } from './context';
import { Field, SelectInput, SettingsCard, StatusBadge } from './controls';
import { Note, ReadingRow } from '../ui';
import { t } from '../../i18n';

// Settings → Network on a board with Ethernet (spec 023 FR-028): the link and
// its addresses, and the network mode, which applies at a restart. Not shown
// on a board without Ethernet; shown in WiFi-only mode (Ethernet not started,
// "Off") so the mode can be changed back.

const MODES: NetworkMode[] = ['auto', 'ethernet', 'wifi'];

export const modeLabel = (mode: NetworkMode) => t(`settings.network.mode.${mode}`);
export const modeDetail = (mode: NetworkMode) => t(`settings.network.modeDetail.${mode}`);

const linkBadge = (ethernet: EthernetStatus) => {
  if (!ethernet.present) return <StatusBadge tone="off" label={t('settings.network.offWifiOnly')} />;
  return ethernet.link ? (
    <StatusBadge tone="ok" label={t('settings.network.connected')} />
  ) : (
    <StatusBadge tone="warn" label={t('settings.network.noLink')} />
  );
};

export const EthernetRows: FunctionalComponent<{ ethernet: EthernetStatus }> = ({ ethernet }) => (
  <div data-ethernet-rows>
    {ethernet.link && (
      <ReadingRow label={t('settings.network.speed')} value={t('settings.network.speedValue', { speed: ethernet.speedMbps })} />
    )}
    <ReadingRow label={t('system.ipAddress')} value={ethernet.ip || '--'} valueClass="mono ltr" />
    {ethernet.mac && <ReadingRow label={t('system.macAddress')} value={ethernet.mac} valueClass="mono ltr" />}
  </div>
);

export const EthernetCard: FunctionalComponent<SettingsTabProps> = ({ config, status, update }) => {
  const ethernet = status?.ethernet;
  const mode = config.network?.mode;
  return (
    <SettingsCard
      id="ethernet"
      title={t('settings.network.ethernet')}
      hint={t('settings.network.ethernetHint')}
      badge={ethernet && linkBadge(ethernet)}
    >
      {ethernet?.present && <EthernetRows ethernet={ethernet} />}
      {mode && (
        <div class="form-grid">
          <Field label={t('settings.network.modeLabel')} hint={t('settings.network.modeHint')}>
            <SelectInput
              dataField="network.mode"
              value={mode}
              options={MODES.map((value) => ({ value, label: modeLabel(value) }))}
              onChange={(value) => update(['network', 'mode'], value)}
            />
          </Field>
        </div>
      )}
      {mode && <Note>{modeDetail(mode)}</Note>}
    </SettingsCard>
  );
};

export default EthernetCard;
