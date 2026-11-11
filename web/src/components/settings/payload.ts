import type { Config } from '../../types';
import { defaultAlpacaConfig, defaultAuthConfig, defaultHomeAssistant, mergeAlertsConfig } from './defaults';

// Normalises a config from the device (or the form) into the full shape the
// UI edits and the firmware accepts.

export const fieldErrorAliases: Record<string, string> = {
  mqttBroker: 'mqtt.broker',
  mqttPort: 'mqtt.port',
  mqttTopic: 'mqtt.topic',
  mqttInterval: 'mqtt.publishIntervalMs',
};

export const toConfigPayload = (source: Config): Config => ({
  deviceName: source.deviceName,
  wifi: { ...source.wifi },
  mqtt: { ...source.mqtt, homeAssistant: { ...defaultHomeAssistant, ...source.mqtt.homeAssistant } },
  ota: { ...source.ota },
  auth: source.auth ? { ...source.auth } : { ...defaultAuthConfig },
  ntp: { ...source.ntp },
  alpaca: { ...defaultAlpacaConfig, ...source.alpaca },
  alerts: mergeAlertsConfig(source.alerts),
  // The roof's sections (spec 009 FR-023): carried when the device sent them, never invented.
  ...(source.safety && { safety: { ...source.safety, http: { ...source.safety.http }, mqtt: { ...source.safety.mqtt } } }),
  ...(source.park && { park: { ...source.park, nina: { ...source.park.nina } } }),
  ...(source.roof && { roof: { ...source.roof } }),
  // The daylight failsafe and the site (spec 013).
  ...(source.daylight && { daylight: { ...source.daylight } }),
  ...(source.location && { location: { ...source.location } }),
});
