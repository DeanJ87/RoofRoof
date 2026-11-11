import type { SettingsTabProps } from '../settings/context';
import type { TestSave } from '../../hooks/useInputsTest';

// What each wizard page gets: the Settings tab props (the config as edited,
// its errors and the live status) plus the wizard's own state.
export interface WizardPageProps extends SettingsTabProps {
  // The owner said the Hall sensor isn't fitted yet (this visit only).
  hallNotFitted: boolean;
  setHallNotFitted: (notFitted: boolean) => void;
  // Saves the page for a Test; `changed` says whether these settings changed.
  saveForTest: (fields: string[]) => TestSave;
  goToPage: (index: number) => void;
  // True once per wizard visit, when no password is saved: page 1 then starts with protection on.
  offerPassword: () => boolean;
}
