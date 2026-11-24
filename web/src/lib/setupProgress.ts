import type { Config, RoofConfig } from '../types';
import { t } from '../i18n';
import { formatNumber } from '../i18n/format';
import { moveLimits } from './moveLimits';

// Spec 026: the roof's setup stages in the order an installer does them, each done
// or not, with the page that does it. Shown on the dashboard until the roof is set up,
// and at the top of Settings → Roof. Derived from the saved settings only.

export type StageState = 'done' | 'todo' | 'bypassed' | 'typed';
export interface Stage {
  id: 'board' | 'weather' | 'park' | 'direction' | 'ends' | 'calibrated' | 'alerts';
  title: string;
  state: StageState;
  note: string; // what is done or still to do, one sentence
  href: string;
}

export const MEASURED = { travel: 1, speed: 2, margin: 4, hall: 8, direction: 16 } as const;

// The device's defaults for travel and speed (lib/ConfigModel RoofConfig).
const DEFAULT_TRAVEL_MM = 1850;
const DEFAULT_SPEED = 38;

const measured = (roof: RoofConfig, bits: number) => ((roof.measured ?? 0) & bits) === bits;

/** Who set a roof value: measured on the roof, the device's default, or typed here. */
export const valueSource = (roof: RoofConfig, bits: number, isDefault: boolean): 'measured' | 'default' | 'typed' =>
  measured(roof, bits) ? 'measured' : isDefault ? 'default' : 'typed';

/** Travel and speed are still the device's defaults: every move's time limit is a guess. */
export const motionDefaults = (roof: RoofConfig) => roof.travelMm === DEFAULT_TRAVEL_MM && roof.speedMmPerSecond === DEFAULT_SPEED;

const calibrated = (roof: RoofConfig): Pick<Stage, 'state' | 'note'> => {
  if (measured(roof, MEASURED.travel | MEASURED.speed)) return { state: 'done', note: t('setupProgress.calibratedDone') };
  const seconds = formatNumber(moveLimits(roof).fullSeconds, 0);
  if (motionDefaults(roof)) return { state: 'todo', note: t('setupProgress.calibratedDefaults', { seconds }) };
  return { state: 'typed', note: t('setupProgress.calibratedTyped', { seconds }) };
};

const sourceStage = (on: boolean, bypassed: boolean, done: string, todo: string): Pick<Stage, 'state' | 'note'> => {
  if (bypassed) return { state: 'bypassed', note: t('setupProgress.bypassed') };
  return on ? { state: 'done', note: done } : { state: 'todo', note: todo };
};

const ROOF = '/settings?tab=roof&section=';

/** The stages, in order; empty when the device sent no roof settings (older firmware). */
export const setupStages = (config: Config | null | undefined): Stage[] => {
  const roof = config?.roof;
  if (!config || !roof) return [];
  const weatherOn = Boolean(config.safety?.http.enabled || config.safety?.mqtt.enabled);
  const parkOn = Boolean(config.park?.nina.url.trim() || config.park?.source === 'switches');
  const alerts = config.alerts;
  const alertsOn = Boolean(alerts?.enabled && (alerts.ntfy.enabled || alerts.pushover.enabled || alerts.webhook.enabled || alerts.mqtt.enabled));
  const stage = (id: Stage['id'], href: string, rest: Pick<Stage, 'state' | 'note'>): Stage => ({
    id,
    title: t(`setupProgress.${id}`),
    href,
    ...rest,
  });
  const done = (ok: boolean, id: 'board' | 'direction' | 'ends' | 'alerts') =>
    ok ? { state: 'done' as const, note: t(`setupProgress.${id}Done`) } : { state: 'todo' as const, note: t(`setupProgress.${id}Todo`) };
  return [
    stage('board', '/pins', done(config.pins?.preset !== 'unset', 'board')),
    stage(
      'weather',
      `${ROOF}safety`,
      sourceStage(weatherOn, Boolean(config.testBox?.weather), t('setupProgress.weatherDone'), t('setupProgress.weatherTodo')),
    ),
    stage('park', `${ROOF}park`, sourceStage(parkOn, Boolean(config.testBox?.park), t('setupProgress.parkDone'), t('setupProgress.parkTodo'))),
    stage('direction', '/calibrate', done(measured(roof, MEASURED.direction), 'direction')),
    stage('ends', '/calibrate', done(measured(roof, MEASURED.hall), 'ends')),
    stage('calibrated', '/calibrate', calibrated(roof)),
    stage('alerts', '/settings?tab=alerts', done(alertsOn, 'alerts')),
  ];
};

/** Every stage done (a test box bypass isn't a set-up roof). */
export const setupComplete = (stages: Stage[]) => stages.every((stage) => stage.state === 'done');
