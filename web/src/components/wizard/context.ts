import type { Config } from '../../types';
import type { SettingsTabProps } from '../settings/context';
import type { TestSave } from '../../hooks/useInputsTest';

// What each wizard screen gets: the Settings tab props (the config as edited,
// its errors and the live status) plus the wizard's own state.
export interface WizardPageProps extends SettingsTabProps {
  // The settings as the device has them (before this screen's edits).
  saved: Config;
  // The owner said the Hall sensor isn't fitted yet (this visit only).
  hallNotFitted: boolean;
  setHallNotFitted: (notFitted: boolean) => void;
  // Saves the screen for a Test; `changed` says whether these settings changed.
  saveForTest: (fields: string[]) => TestSave;
  // Saves the screen without moving on (Send test needs the saved channel).
  saveHere: () => void;
  // Opens the first screen of spec 015's page `index` (the review's Change).
  goToPage: (index: number) => void;
  // True once per wizard visit, when no password is saved: protection then starts on.
  offerPassword: () => boolean;
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
