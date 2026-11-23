import type { Config, RoofConfig } from '../../types';
import type { SettingsTabProps } from '../settings/context';
import type { TestSave } from '../../hooks/useInputsTest';
import type { ScreenId } from './screens';

// What each wizard screen gets: the Settings tab props (the config as edited,
// its errors and the live status) plus the wizard's own state. Spec 025: no
// setting's answer is kept here; every screen reads the settings.
export interface WizardPageProps extends SettingsTabProps {
  // The settings as the device has them (before this screen's edits).
  saved: Config;
  // Saves the screen for a Test; `changed` says whether these settings changed.
  saveForTest: (fields: string[]) => TestSave;
  // Saves the screen without moving on (Send test needs the saved channel).
  saveHere: () => void;
  // Opens the first screen of part `index` (the review's Change), saving this screen first.
  goToPage: (index: number) => void | Promise<void>;
  // Opens a screen by id (Edit, "Enter manually instead"), saving this screen first.
  goToScreen: (id: ScreenId) => void | Promise<void>;
  // Leaves the wizard for `url` once this screen is saved (or its part recorded); it resumes there.
  leaveTo: (url: string) => void | Promise<void>;
  // Reads the settings again after the device saved some itself (a confirmed teach).
  reload: () => void;
  // Saves an accepted calibration proposal; null when saved, else why not.
  acceptRoof: (roof: RoofConfig) => Promise<string | null>;
  // Live status recent enough to start a jog or a run on (as the Calibrate page).
  fresh: boolean;
  // The direction check's answer in this visit (a confirmation, never saved).
  direction: 'yes' | 'no' | null;
  setDirection: (answer: 'yes' | 'no') => void;
  // The id of the screen's question, which names a choice group.
  questionId: string;
  // A choice screen shows its current setting as chosen (spec 018 FR-009).
  chosen: boolean;
  // The owner made a choice on this screen.
  choose: () => void;
}

// A choice group's props for one screen: nothing selected until chosen.
export const choiceOf = (props: WizardPageProps, name: string, current: string | null, set: (value: string) => void) => ({
  name,
  labelledBy: props.questionId,
  value: props.chosen ? current : null,
  onChange: (value: string) => {
    set(value);
    props.choose();
  },
});

/**
 * MQTT on while a screen's MQTT use is on (FR-007: its broker fields then show on that screen),
 * otherwise as the device had it unless another use still needs it.
 */
export const mqttChange = (props: WizardPageProps, on: boolean, otherUse: boolean): [string[], unknown] => [
  ['mqtt', 'enabled'],
  on || otherUse || props.saved.mqtt.enabled,
];
