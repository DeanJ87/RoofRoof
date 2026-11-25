import type { Config, RoofInputs, SystemStatus } from '../../types';
import { t } from '../../i18n';
import { formatDateTime, formatNumber } from '../../i18n/format';
import type { WizardPageId } from './pages';
import { pinsRestartNeeded, presetLabel } from '../../lib/pinsView';
import { ninaCounts, parkSource } from '../../lib/parkSource';

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

// Spec 024: names the park source; one switch alone is attention.
const parkSwitches = ({ config, status }: ReviewInput): Check => {
  const source = config.park ? parkSource(config.park) : 'nina';
  if (source === 'nina' || config.park?.switches === 'none') return warn(t('wizard.review.noParkSwitches'));
  const switches = status?.roof?.parkSwitches;
  if (!switches) return unknown();
  const readings = [switches.switch1, switches.switch2].slice(0, switches.fitted);
  if (readings.includes('unreadable')) return bad(t('wizard.review.parkSwitchUnreadable'));
  if (source === 'ninaAndSwitches') return ok(t('wizard.review.parkBothRead'));
  return config.park?.switches === 'one' ? warn(t('wizard.review.parkOneSwitchOnly')) : ok(t('wizard.review.parkSwitchesOnlyRead'));
};

// Spec 023: a board chosen and its pins in effect and valid.
const board = ({ config, status }: ReviewInput): Check => {
  if (config.pins?.preset === 'unset') return bad(t('wizard.review.noBoard'));
  const pins = status?.pins;
  if (!pins) return unknown();
  if (pinsRestartNeeded(config.pins, status)) return warn(t('wizard.review.boardRestart'));
  if (!pins.valid) return bad(t('wizard.review.pinsInvalid'));
  return ok(t('wizard.review.boardOk', { board: presetLabel(pins.preset) }));
};

// Spec 025: from the live reading only (no "not fitted" answer is kept); a sensor not connected reads at a rail.
const hall = ({ status }: ReviewInput): Check => {
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
  if (!config.safety?.http.enabled && !config.safety?.mqtt.enabled)
    return config.testBox?.weather ? warn(t('wizard.review.weatherBypassed')) : bad(t('wizard.review.noWeatherSource'));
  if (!inputs) return unknown();
  if (inputs.weather.verdict === 'safe') return ok(t('wizard.review.weatherSafe'));
  return warn(t('wizard.review.weatherNotSafe', { text: inputs.weather.text }));
};

const nina = ({ config, inputs }: ReviewInput): Check => {
  if (!config.park?.nina.url) {
    if (config.park && !ninaCounts(config.park)) return ok(t('wizard.review.ninaOptional'));
    return config.testBox?.park ? warn(t('wizard.review.parkBypassed')) : bad(t('wizard.review.noNina'));
  }
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
  if (!a?.enabled || channels === 0) return bad(t('wizard.alerts.noChannel'));
  return ok(t('wizard.review.alertChannels', { count: channels }));
};

const daylight = ({ config }: ReviewInput): Check => {
  const mode = config.daylight?.mode;
  if (!mode) return unknown();
  if (mode === 'off') return warn(t('wizard.review.daylightOff'));
  return ok(mode === 'close' ? t('wizard.review.daylightCloses') : t('wizard.review.daylightAlerts'));
};

// Spec 022: the bypasses are never "OK".
const testBox = ({ config, status }: ReviewInput): Check => {
  const box = config.testBox;
  if (!box || !(box.park || box.weather || box.daylight)) return ok(t('wizard.review.testBoxOff'));
  const live = status?.roof?.testBox;
  if (live?.active && live.until > 0) {
    const until = formatDateTime(new Date(live.until * 1000), { dateStyle: 'medium', timeStyle: 'short' });
    return bad(t('wizard.review.testBoxUntil', { until }));
  }
  return bad(t('wizard.review.testBoxOn', { hours: box.hours }));
};

const CHECKS: [string, string, WizardPageId, (input: ReviewInput) => Check][] = [
  ['testBox', 'wizard.review.testBox', 'testBox', testBox],
  ['board', 'wizard.review.board', 'board', board],
  ['supply', 'wizard.review.supply', 'wiring', supply],
  ['weather', 'wizard.review.weather', 'sqmeter', weather],
  ['site', 'wizard.review.site', 'sqmeter', site],
  ['clock', 'wizard.review.clock', 'sqmeter', clock],
  ['daylight', 'wizard.review.daylight', 'sqmeter', daylight],
  ['parkSwitches', 'wizard.review.parkSwitches', 'park', parkSwitches],
  ['nina', 'wizard.review.nina', 'park', nina],
  ['alpaca', 'wizard.review.alpaca', 'park', alpaca],
  ['hall', 'wizard.review.hall', 'teach', hall],
  ['motion', 'wizard.review.motion', 'calibrate', motion],
  ['alerts', 'wizard.review.alerts', 'alerts', alerts],
  ['password', 'wizard.review.password', 'password', password],
];

export const reviewChecks = (input: ReviewInput): ReviewCheck[] =>
  CHECKS.map(([id, label, page, check]) => ({ id, label: t(label as Parameters<typeof t>[0]), page, ...check(input) }));
