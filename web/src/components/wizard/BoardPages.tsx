import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useEffect } from 'preact/hooks';
import type { NetworkMode, PinsConfig } from '../../types';
import type { WizardPageProps } from './context';
import { usePinsDocument, usePinValidation } from '../../hooks/usePins';
import { pinsRestartNeeded, suggestedPreset, withPreset, wt32Detected } from '../../lib/pinsView';
import { IssueNotes, PresetChoice, UnsetNotes } from '../pins/PinFields';
import { WiringCheckPanel } from '../pins/WiringCheck';
import { modeDetail, modeLabel } from '../settings/EthernetCard';
import { restartWithToast } from '../toast';
import { Button, ChoiceGroup, Note } from '../ui';
import { t } from '../../i18n';

// The Hardware page's board, network and wiring screens (spec 023 FR-027).
// The board is saved like any screen and applies at a restart; the wiring
// check moves nothing. Custom pins are set on the Pins page.

// A refused restart says why (restartWithToast); nothing waits for one that didn't happen.
const restart = () => void restartWithToast();

/** The saved board isn't the one in effect: a restart applies it. */
export const PinsRestartNote: FunctionalComponent<WizardPageProps> = ({ saved, status }) =>
  pinsRestartNeeded(saved.pins, status) ? (
    <Note tone="warn" action={{ label: t('settings.restart'), onClick: restart }}>
      {t('wizard.board.restart')}
    </Note>
  ) : null;

export const BoardScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { doc } = usePinsDocument();
  const pins = props.config.pins;
  const unset = props.saved.pins?.preset === 'unset';
  // The device's verdict on the board as chosen: an error (a board it found to be a WT32-ETH01
  // set up as another) is shown here, and the device refuses to save it.
  const check = usePinValidation(pins);
  // A board preset chosen. The analog setup doesn't follow it: the pins apply at a restart and
  // the analog setup at once, and the board preset can't tell a carrier v1 from a v1.1 (its diode).
  const choose = (next: PinsConfig) => props.updateMany([[['pins'], next]]);
  // A new device: the board its boot probe suggests is chosen for the person to confirm.
  useEffect(() => {
    if (doc && unset && pins?.preset === 'unset') choose(withPreset(doc, pins, suggestedPreset(doc)));
    // eslint-disable-next-line react-hooks/exhaustive-deps -- once, when the device's pin document arrives
  }, [doc]);
  if (!pins) return <Note>{t('settings.roof.notSent')}</Note>;
  return (
    <>
      {unset && <UnsetNotes doc={doc} />}
      <PresetChoice
        doc={doc}
        pins={pins}
        labelledBy={props.questionId}
        newDevice={unset}
        wt32={wt32Detected(doc, props.status)}
        onChange={choose}
      />
      <IssueNotes issues={(check.validation?.issues ?? []).filter((issue) => issue.error)} idPrefix="wizard-pin-issue" />
      {pins.preset === 'custom' && (
        <>
          <Note>{t('wizard.board.custom')}</Note>
          <div class="btn-row">
            <Button onClick={() => route('/pins')}>{t('pins.open')}</Button>
          </div>
        </>
      )}
      {pins.preset !== 'unset' && pins.preset !== props.saved.pins?.preset && <Note>{t('wizard.board.appliesAtRestart')}</Note>}
      <PinsRestartNote {...props} />
    </>
  );
};

const MODE_ICON: Record<NetworkMode, string> = { auto: 'link', ethernet: 'link', wifi: 'wifi' };

export const NetworkScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const ethernet = props.status?.ethernet;
  const network = props.config.network;
  return (
    <>
      {ethernet?.link && ethernet.ip && <Note tone="ok">{t('wizard.network.onEthernet', { ip: ethernet.ip })}</Note>}
      {network ? (
        <ChoiceGroup
          name="network-mode"
          labelledBy={props.questionId}
          value={network.mode}
          onChange={(mode) => props.update(['network', 'mode'], mode)}
          options={(['auto', 'ethernet', 'wifi'] as NetworkMode[]).map((mode) => ({
            value: mode,
            icon: MODE_ICON[mode],
            title: modeLabel(mode),
            detail: modeDetail(mode),
            badge: mode === 'auto' ? t('wizard.recommended') : undefined,
          }))}
        />
      ) : (
        <Note>{t('settings.roof.notSent')}</Note>
      )}
      {network && network.mode !== props.saved.network?.mode && <Note>{t('wizard.network.appliesAtRestart')}</Note>}
      <Note>{t('wizard.network.wifiOptional')}</Note>
    </>
  );
};

export const WiringScreen: FunctionalComponent<WizardPageProps> = (props) => {
  const { doc } = usePinsDocument();
  const pins = props.config.pins;
  return (
    <>
      <PinsRestartNote {...props} />
      <WiringCheckPanel doc={doc} onChoosePreset={pins ? (preset) => props.update(['pins'], withPreset(doc, pins, preset)) : undefined} />
      <Note>{t('wizard.wiring.later')}</Note>
    </>
  );
};
