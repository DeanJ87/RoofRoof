import { t } from '../i18n';

// Why a dependent setting is inactive (SQMeter spec 020), for
// lib/settingsDeps.ts. Same codes, texts and fix targets as lib/SettingsDeps/catalogue.json (a
// test compares them).
export const REASONS: Record<string, { text: string; fix: string }> = {
  'alerts-off': { text: t('settingsDeps.alertsAreOff'), fix: 'alerts#alerts' },
  'mqtt-off': { text: t('settingsDeps.mqttIsOff'), fix: 'network#mqtt' },
  'mqtt-disconnected': { text: t('settingsDeps.brokerNotConnected'), fix: 'network#mqtt' },
  'wifi-disconnected': { text: t('settingsDeps.notConnectedToWifi'), fix: 'network#wifi' },
  'alpaca-off': { text: t('settingsDeps.alpacaIsOff'), fix: 'network#alpaca' },
  'no-alpaca-device': { text: t('settingsDeps.noAlpacaDevice'), fix: 'network#alpaca' },
  'ota-no-password': { text: t('settingsDeps.setAnUploadPassword'), fix: 'device#security' },
};

// Label for the one-click fix of each reason.
export const FIX_LABEL: Record<string, string> = {
  'alerts-off': t('settingsDeps.turnOnAlerts'),
  'mqtt-off': t('settingsDeps.turnOnMqtt'),
  'mqtt-disconnected': t('settingsDeps.mqttSettings'),
  'wifi-disconnected': t('settingsDeps.wifiSettings'),
  'alpaca-off': t('settingsDeps.turnOnAlpaca'),
  'no-alpaca-device': t('settingsDeps.alpacaSettings'),
  'ota-no-password': t('settingsDeps.setPassword'),
};
