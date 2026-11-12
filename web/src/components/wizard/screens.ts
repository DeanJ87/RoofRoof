import type { FunctionalComponent } from 'preact';
import type { WizardPageProps } from './context';
import { WIZARD_PAGES, pageIndex, type WizardPageId } from './pages';
import { NameScreen, PasswordScreen, SiteScreen, TimeScreen } from './DevicePages';
import {
  CalibrationScreen,
  CurrentScreen,
  HallFittedScreen,
  HallScreen,
  MotionScreen,
  ParkSwitchesScreen,
  RainScreen,
  SupplyScreen,
} from './HardwarePages';
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
  | 'site'
  | 'time'
  | 'supply'
  | 'current'
  | 'parkSwitches'
  | 'rain'
  | 'hallFitted'
  | 'hall'
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
const hasSource = (p: WizardPageProps) => Boolean(p.config.safety?.http.enabled || p.config.safety?.mqtt.enabled);
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
  { id: 'site', page: 'site', Body: SiteScreen },
  { id: 'time', page: 'site', Body: TimeScreen },
  { id: 'supply', page: 'hardware', Body: SupplyScreen },
  { id: 'current', page: 'hardware', Body: CurrentScreen, choice: true },
  { id: 'parkSwitches', page: 'hardware', Body: ParkSwitchesScreen, choice: true },
  { id: 'rain', page: 'hardware', Body: RainScreen, choice: true },
  { id: 'hallFitted', page: 'hall', Body: HallFittedScreen, choice: true },
  { id: 'hall', page: 'hall', Body: HallScreen, shows: hallFitted },
  { id: 'calibrate', page: 'calibrate', Body: CalibrationScreen },
  { id: 'motion', page: 'motion', Body: MotionScreen },
  { id: 'weatherSource', page: 'weather', Body: WeatherSourceScreen, choice: true, answered: hasSource },
  { id: 'weatherAddress', page: 'weather', Body: WeatherAddressScreen, shows: hasSource },
  { id: 'nina', page: 'nina', Body: NinaScreen },
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
