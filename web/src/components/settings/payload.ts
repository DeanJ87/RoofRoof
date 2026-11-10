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
});
