import type { FunctionalComponent } from 'preact';
import type { WizardPageProps } from './context';
import { WIZARD_PAGES, pageIndex, type WizardPageId } from './pages';
import { NameScreen, PasswordScreen, SiteScreen, SqmeterScreen, TimeScreen } from './DevicePages';
import {
  CalibrationScreen,
  TeachScreen,
  CurrentScreen,
  HallFittedScreen,
  HallScreen,
  MotionScreen,
  ParkSwitchCountScreen,
  ParkSwitchesScreen,
  TestBoxScreen,
  SupplyScreen,
} from './HardwarePages';
import { BoardScreen, NetworkScreen, WiringScreen } from './BoardPages';
import { hasEthernet } from '../../lib/pinsView';
import { parkSource } from '../../lib/parkSource';
import {
  AlertChannelScreen,
  AlertDetailsScreen,
  AlpacaScreen,
  DaylightScreen,
  NinaScreen,
  WeatherAddressScreen,
  WeatherSourceScreen,
} from './SourcePages';
import { ReviewScreen } from './ReviewPage';

// The wizard's screens (spec 018 FR-007): one question each, grouped under
// spec 015's pages, whose index stays `setup.step` (the resume point).

export type ScreenId =
  | 'name'
  | 'password'
  | 'sqmeter'
  | 'site'
  | 'time'
  | 'board'
  | 'network'
  | 'testBox'
  | 'supply'
  | 'current'
  | 'parkSwitches'
  | 'parkSwitchCount'
  | 'wiring'
  | 'hallFitted'
  | 'hall'
  | 'teach'
  | 'calibrate'
  | 'motion'
  | 'weatherSource'
  | 'weatherAddress'
  | 'nina'
  | 'alpaca'
  | 'alertChannel'
  | 'alertDetails'
  | 'daylight'
  | 'review';

export interface Screen {
  id: ScreenId;
  page: WizardPageId;
  Body: FunctionalComponent<WizardPageProps>;
  // A choice screen: answered once a choice shows (FR-006, FR-009).
  choice?: boolean;
  // Left out (and out of the progress count) while false.
  shows?: (props: WizardPageProps) => boolean;
  // Continue waits for this (besides a choice).
  answered?: (props: WizardPageProps) => boolean;
}

const hallFitted = (p: WizardPageProps) => !p.hallNotFitted;
const boardChosen = (p: WizardPageProps) => !p.config.pins || p.config.pins.preset !== 'unset';
const hasSource = (p: WizardPageProps) => Boolean(p.config.safety?.http.enabled || p.config.safety?.mqtt.enabled);
// Spec 022: a test box skips the sources its bypasses stand in for.
const noParkBypass = (p: WizardPageProps) => !p.config.testBox?.park;
const noWeatherBypass = (p: WizardPageProps) => !p.config.testBox?.weather;
const usesSwitches = (p: WizardPageProps) => Boolean(p.config.park && parkSource(p.config.park) !== 'nina');
const alertsOn = (p: WizardPageProps) => {
  const a = p.config.alerts;
  return Boolean(a?.enabled && (a.ntfy.enabled || a.pushover.enabled || a.webhook.enabled || a.mqtt.enabled));
};

export const SCREENS: Screen[] = [
  { id: 'name', page: 'device', Body: NameScreen, answered: (p) => p.config.deviceName.trim() !== '' },
  {
    id: 'password',
    page: 'device',
    Body: PasswordScreen,
    choice: true,
    answered: (p) => !p.config.auth.enabled || p.config.auth.password !== '',
  },
  // Spec 020: SQMeter's address and the copy of its site settings, first on the page.
  { id: 'sqmeter', page: 'site', Body: SqmeterScreen },
  { id: 'site', page: 'site', Body: SiteScreen },
  { id: 'time', page: 'site', Body: TimeScreen },
  // Spec 023: the board first (nothing is driven until one is chosen), the
  // network mode on a board with Ethernet, and the wiring check last.
  { id: 'board', page: 'hardware', Body: BoardScreen, answered: boardChosen },
  { id: 'network', page: 'hardware', Body: NetworkScreen, shows: (p) => hasEthernet(p.status) },
  // Spec 022: is there a telescope under this roof yet? (after the board, which drives nothing until chosen)
  { id: 'testBox', page: 'hardware', Body: TestBoxScreen, choice: true },
  { id: 'supply', page: 'hardware', Body: SupplyScreen },
  { id: 'current', page: 'hardware', Body: CurrentScreen, choice: true },
  // Spec 024: how the park is known, then how many switches (a switch source only).
  { id: 'parkSwitches', page: 'hardware', Body: ParkSwitchesScreen, choice: true },
  { id: 'parkSwitchCount', page: 'hardware', Body: ParkSwitchCountScreen, choice: true, shows: usesSwitches },
  { id: 'wiring', page: 'hardware', Body: WiringScreen },
  { id: 'hallFitted', page: 'hall', Body: HallFittedScreen, choice: true },
  { id: 'hall', page: 'hall', Body: HallScreen, shows: hallFitted },
  { id: 'teach', page: 'teach', Body: TeachScreen },
  { id: 'calibrate', page: 'calibrate', Body: CalibrationScreen },
  { id: 'motion', page: 'motion', Body: MotionScreen },
  { id: 'weatherSource', page: 'weather', Body: WeatherSourceScreen, choice: true, answered: hasSource, shows: noWeatherBypass },
  { id: 'weatherAddress', page: 'weather', Body: WeatherAddressScreen, shows: (p) => hasSource(p) && noWeatherBypass(p) },
  { id: 'nina', page: 'nina', Body: NinaScreen, shows: noParkBypass },
  { id: 'alpaca', page: 'nina', Body: AlpacaScreen, choice: true },
  { id: 'alertChannel', page: 'alerts', Body: AlertChannelScreen, choice: true, answered: alertsOn },
  { id: 'alertDetails', page: 'alerts', Body: AlertDetailsScreen, shows: alertsOn },
  { id: 'daylight', page: 'daylight', Body: DaylightScreen, choice: true },
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

/** The first screen of spec 015's page `page` (an index into WIZARD_PAGES). */
export const firstScreenOf = (page: number): number => {
  const id = WIZARD_PAGES[page]?.id ?? 'device';
  return Math.max(
    0,
    SCREENS.findIndex((screen) => screen.page === id),
  );
};

/** The page a screen saves as `setup.step`. */
export const stepOf = (index: number): number => pageIndex(SCREENS[index].page);

/** Continue is available: the choice made (when a choice screen) and the screen's own rule. */
export const isAnswered = (index: number, props: WizardPageProps): boolean => {
  const screen = SCREENS[index];
  if (screen.choice && !props.chosen) return false;
  return screen.answered ? screen.answered(props) : true;
};
