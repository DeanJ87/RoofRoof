import type { Config, RoofInputs, SystemStatus } from '../../types';
import { t } from '../../i18n';
import { formatNumber } from '../../i18n/format';
import type { WizardPageId } from './pages';

// The review page's checks (spec 015 FR-010): one per essential, from the
// saved settings and the live readings. A missing reading is "unknown", never OK.

export type CheckState = 'ok' | 'warn' | 'bad' | 'unknown';

export interface ReviewCheck {
  id: string;
  label: string;
  state: CheckState;
  text: string;
  page: WizardPageId;
}

export interface ReviewInput {
  config: Config;
  status: SystemStatus | null;
  inputs: RoofInputs | null;
  hallNotFitted: boolean;
}

type Check = Omit<ReviewCheck, 'id' | 'label' | 'page'>;
const ok = (text: string): Check => ({ state: 'ok', text });
const warn = (text: string): Check => ({ state: 'warn', text });
const bad = (text: string): Check => ({ state: 'bad', text });
const unknown = (): Check => ({ state: 'unknown', text: t('wizard.review.noReading') });

const DEFAULT_TRAVEL_MM = 1850;
const DEFAULT_SPEED = 38;

const password = ({ config }: ReviewInput): Check =>
  config.auth.enabled && config.auth.password !== '' ? ok(t('wizard.review.passwordSet')) : bad(t('wizard.review.noPassword'));

const site = ({ config }: ReviewInput): Check => {
  if (config.location?.set) return ok(t('wizard.review.siteSet'));
  return config.daylight?.mode === 'off' ? warn(t('wizard.review.siteNotSet')) : bad(t('wizard.review.siteNeededForDaylight'));
};

const clock = ({ status }: ReviewInput): Check => {
  if (!status) return unknown();
  return status.time.epoch > 0 ? ok(t('wizard.review.clockSet')) : warn(t('wizard.review.clockNotSet'));
};

const supply = ({ status }: ReviewInput): Check => {
  const roof = status?.roof;
  if (!roof) return unknown();
  if (roof.supplyVolts === null) return bad(t('wizard.review.supplyUnreadable'));
  const volts = formatNumber(roof.supplyVolts, 1);
  if (roof.supplyLow) return bad(t('wizard.review.supplyLow', { volts, limit: formatNumber(roof.minSupplyVolts, 1) }));
  return ok(t('wizard.review.supplyOk', { volts }));
};

const parkSwitches = ({ config, status }: ReviewInput): Check => {
  if ((config.park?.switches ?? 'none') === 'none') return warn(t('wizard.review.noParkSwitches'));
  const switches = status?.roof?.parkSwitches;
  if (!switches) return unknown();
  const readings = [switches.switch1, switches.switch2].slice(0, switches.fitted);
  return readings.includes('unreadable') ? bad(t('wizard.review.parkSwitchUnreadable')) : ok(t('wizard.review.parkSwitchesRead'));
};

const rainContact = ({ config, status }: ReviewInput): Check => {
  if (!config.safety?.rainContact) return ok(t('wizard.review.noRainContact'));
  const reading = status?.roof?.rainContact;
  if (!reading) return unknown();
  return reading === 'unreadable' ? bad(t('wizard.review.rainContactUnreadable')) : ok(t('wizard.review.rainContactRead'));
};

const hall = ({ status, hallNotFitted }: ReviewInput): Check => {
  if (hallNotFitted) return bad(t('wizard.review.hallNotFitted'));
  const level = status?.roof?.hall.level;
  if (!level) return unknown();
  if (level === 'rail') return bad(t('wizard.review.hallRail'));
  return level === 'low' || level === 'high' ? ok(t('wizard.review.hallAtMagnet')) : warn(t('wizard.review.hallBetween'));
};

const motion = ({ config }: ReviewInput): Check => {
  const roof = config.roof;
  if (!roof) return unknown();
  if (roof.travelMm === DEFAULT_TRAVEL_MM && roof.speedMmPerSecond === DEFAULT_SPEED) return warn(t('wizard.review.motionNotMeasured'));
  return ok(t('wizard.review.motionSet', { duty: roof.topDutyPercent }));
};

const weather = ({ config, inputs }: ReviewInput): Check => {
  if (!config.safety?.http.enabled && !config.safety?.mqtt.enabled) return bad(t('wizard.review.noWeatherSource'));
  if (!inputs) return unknown();
  if (inputs.weather.verdict === 'safe') return ok(t('wizard.review.weatherSafe'));
  return warn(t('wizard.review.weatherNotSafe', { text: inputs.weather.text }));
};

const nina = ({ config, inputs }: ReviewInput): Check => {
  if (!config.park?.nina.url) return bad(t('wizard.review.noNina'));
  if (!inputs) return unknown();
  if (inputs.nina.state !== 'good') return warn(t('wizard.review.ninaNotAnswering', { text: inputs.nina.text || inputs.nina.state }));
  return inputs.nina.connected ? ok(t('wizard.review.ninaConnected')) : warn(t('wizard.review.mountNotConnected'));
};

const alpaca = ({ config }: ReviewInput): Check =>
  config.alpaca?.enabled ? ok(t('wizard.review.alpacaOn')) : warn(t('wizard.review.alpacaOff'));

const alerts = ({ config }: ReviewInput): Check => {
  const a = config.alerts;
  const channels = a
    ? [a.pushover.enabled, a.ntfy.enabled, a.webhook.enabled, a.mqtt.enabled && config.mqtt.enabled].filter(Boolean).length
    : 0;
  if (!a?.enabled || channels === 0) return bad(t('wizard.review.noAlertChannel'));
  return ok(t('wizard.review.alertChannels', { count: channels }));
};

const daylight = ({ config }: ReviewInput): Check => {
  const mode = config.daylight?.mode;
  if (!mode) return unknown();
  if (mode === 'off') return warn(t('wizard.review.daylightOff'));
  return ok(mode === 'close' ? t('wizard.review.daylightCloses') : t('wizard.review.daylightAlerts'));
};

const CHECKS: [string, string, WizardPageId, (input: ReviewInput) => Check][] = [
  ['password', 'wizard.review.password', 'device', password],
  ['site', 'wizard.review.site', 'site', site],
  ['clock', 'wizard.review.clock', 'site', clock],
  ['supply', 'wizard.review.supply', 'hardware', supply],
  ['parkSwitches', 'wizard.review.parkSwitches', 'hardware', parkSwitches],
  ['rainContact', 'wizard.review.rainContact', 'hardware', rainContact],
  ['hall', 'wizard.review.hall', 'hall', hall],
  ['motion', 'wizard.review.motion', 'motion', motion],
  ['weather', 'wizard.review.weather', 'weather', weather],
  ['nina', 'wizard.review.nina', 'nina', nina],
  ['alpaca', 'wizard.review.alpaca', 'nina', alpaca],
  ['alerts', 'wizard.review.alerts', 'alerts', alerts],
  ['daylight', 'wizard.review.daylight', 'daylight', daylight],
];

export const reviewChecks = (input: ReviewInput): ReviewCheck[] =>
  CHECKS.map(([id, label, page, check]) => ({ id, label: t(label as Parameters<typeof t>[0]), page, ...check(input) }));
