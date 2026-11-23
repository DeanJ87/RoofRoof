import type { FunctionalComponent } from 'preact';
import type { WizardPageProps } from './context';
import { WIZARD_PAGES, pageIndex, type WizardPageId } from './pages';
import { NameScreen, PasswordScreen, SiteScreen, SqmeterScreen, siteFromSqmeter } from './DevicePages';
import { MotionScreen, ParkSwitchesScreen, TestBoxScreen } from './HardwarePages';
import { BoardScreen, NetworkScreen, WiringScreen } from './BoardPages';
import { CalibrationScreen, DirectionScreen, TeachScreen } from './MotorPages';
import { hasEthernet } from '../../lib/pinsView';
import { AlertsScreen, DaylightScreen, NinaScreen, alertChannelsOn } from './SourcePages';
import { ReviewScreen } from './ReviewPage';

// The wizard's screens (specs 018 and 025): one question each, in the
// installer's order, grouped under the parts whose index is `setup.step`.
// Whether a screen shows comes from the settings and the device's status only,
// never from anything kept in the open page, so a reload asks the same things.

export type ScreenId =
  | 'name'
  | 'network'
  | 'board'
  | 'wiring'
  | 'testBox'
  | 'sqmeter'
  | 'site'
  | 'daylight'
  | 'parkSwitches'
  | 'nina'
  | 'direction'
  | 'teach'
  | 'calibrate'
  | 'motion'
  | 'alerts'
  | 'password'
  | 'review';

export interface Screen {
  id: ScreenId;
  page: WizardPageId;
  Body: FunctionalComponent<WizardPageProps>;
  // A choice screen: answered once a choice shows (FR-006, FR-009).
  choice?: boolean;
  // Left out (and out of the progress count) while false; it still opens when asked for by id.
  shows?: (props: WizardPageProps) => boolean;
  // Continue waits for this (besides a choice).
  answered?: (props: WizardPageProps) => boolean;
}

const boardChosen = (p: WizardPageProps) => !p.config.pins || p.config.pins.preset !== 'unset';
// Spec 022: a test box skips N.I.N.A., which its park bypass stands in for.
const noParkBypass = (p: WizardPageProps) => !p.config.testBox?.park;
const passwordAnswered = (p: WizardPageProps) => !p.config.auth.enabled || p.config.auth.password !== '';

export const SCREENS: Screen[] = [
  { id: 'name', page: 'network', Body: NameScreen, answered: (p) => p.config.deviceName.trim() !== '' },
  { id: 'network', page: 'network', Body: NetworkScreen, shows: (p) => hasEthernet(p.status) },
  // Spec 023: nothing is driven until a board is chosen.
  { id: 'board', page: 'board', Body: BoardScreen, answered: boardChosen },
  { id: 'wiring', page: 'wiring', Body: WiringScreen },
  // Spec 022: is there a telescope under this roof yet?
  { id: 'testBox', page: 'testBox', Body: TestBoxScreen, choice: true },
  // Spec 020: SQMeter once (address, login, copy, weather); what it supplies isn't asked again.
  { id: 'sqmeter', page: 'sqmeter', Body: SqmeterScreen },
  { id: 'site', page: 'sqmeter', Body: SiteScreen, shows: (p) => !siteFromSqmeter(p.config) },
  { id: 'daylight', page: 'sqmeter', Body: DaylightScreen, choice: true },
  { id: 'parkSwitches', page: 'park', Body: ParkSwitchesScreen, choice: true },
  { id: 'nina', page: 'park', Body: NinaScreen, shows: noParkBypass },
  // Specs 019 and 016 inside the wizard: each move attended, every gate kept.
  { id: 'direction', page: 'direction', Body: DirectionScreen, choice: true, answered: (p) => p.direction !== 'no' },
  { id: 'teach', page: 'teach', Body: TeachScreen },
  { id: 'calibrate', page: 'calibrate', Body: CalibrationScreen },
  // "Enter manually instead": opened from the motor screens only.
  { id: 'motion', page: 'calibrate', Body: MotionScreen, shows: () => false },
  // Several channels or none: answered once one is on or the person ticked something.
  { id: 'alerts', page: 'alerts', Body: AlertsScreen, answered: (p) => p.chosen || alertChannelsOn(p).length > 0 },
  { id: 'password', page: 'password', Body: PasswordScreen, choice: true, answered: passwordAnswered },
  { id: 'review', page: 'review', Body: ReviewScreen },
];

export const screenIndex = (id: string): number => SCREENS.findIndex((screen) => screen.id === id);

const showing = (index: number, props: WizardPageProps) => !SCREENS[index].shows || SCREENS[index].shows!(props);

/** The next screen that shows after `index` (the review always shows). */
export const nextScreen = (index: number, props: WizardPageProps): number => {
  for (let i = index + 1; i < SCREENS.length; i++) if (showing(i, props)) return i;
  return SCREENS.length - 1;
};

/** The screen before `index` that shows, or -1 on the first. */
export const previousScreen = (index: number, props: WizardPageProps): number => {
  for (let i = index - 1; i >= 0; i--) if (showing(i, props)) return i;
  return -1;
};

/** `index` if it shows, else the next that does. */
export const settle = (index: number, props: WizardPageProps): number => (showing(index, props) ? index : nextScreen(index, props));

/** Where `index` stands among the screens that show: [position from 1, how many]. */
export const progressOf = (index: number, props: WizardPageProps): [number, number] => {
  const shown = SCREENS.map((_, i) => i).filter((i) => showing(i, props));
  // A screen opened directly that wouldn't show counts where it stands.
  const extra = shown.includes(index) ? 0 : 1;
  return [shown.filter((i) => i < index).length + 1, shown.length + extra];
};

/** The first screen of part `page` (an index into WIZARD_PAGES). */
export const firstScreenOf = (page: number): number => {
  const id = WIZARD_PAGES[page] ?? 'network';
  return Math.max(
    0,
    SCREENS.findIndex((screen) => screen.page === id),
  );
};

/** The part a screen saves as `setup.step`. */
export const stepOf = (index: number): number => pageIndex(SCREENS[index].page);

/** Continue is available: the choice made (when a choice screen) and the screen's own rule. */
export const isAnswered = (index: number, props: WizardPageProps): boolean => {
  const screen = SCREENS[index];
  if (screen.choice && !props.chosen) return false;
  return screen.answered ? screen.answered(props) : true;
};
