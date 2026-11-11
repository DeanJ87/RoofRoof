import { t } from '../../i18n';
// What each dependent setting is called in Settings, for the "Saving makes
// these inactive" preview (SQMeter spec 020).
export const DEP_LABELS: Record<string, string> = {
  'alerts.pushover.enabled': t('settings.depLabels.pushoverAlerts'),
  'alerts.ntfy.enabled': t('settings.depLabels.ntfyAlerts'),
  'alerts.webhook.enabled': t('settings.depLabels.webhookAlerts'),
  'alerts.mqtt.enabled': t('settings.depLabels.mqttAlerts'),
  'alerts.sendMode': t('settings.depLabels.onlyWhileAnImagingApp'),
  'alerts.events.client_lost.level': t('settings.depLabels.theImagingAppStopsChecking'),
  'alerts.events.client_back.level': t('settings.depLabels.theImagingAppIsBack'),
  'alerts.events.client_disconnected.level': t('settings.depLabels.theImagingAppDisconnectsAlerts'),
  'mqtt.homeAssistant.enabled': t('settings.depLabels.homeAssistantDiscovery'),
  'mqtt.homeAssistant.alertsSwitch': t('settings.depLabels.homeAssistantAlertsSwitch'),
  'ntp.enabled': t('settings.depLabels.internetTimeNtp'),
  'ota.enabled': t('settings.depLabels.commandLineUploads'),
  'wifi.mdns': 'mDNS',
  'wifi.ipv6': 'IPv6',
};
