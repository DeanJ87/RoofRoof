import { z } from 'zod';
import { t } from '../i18n';
import { alertsConfigSchema } from './alertsSchema';
import { isIpv6Literal, ntpIpv6Text, parseHost } from '../lib/netAddress';

export const wifiConfigSchema = z.object({
  // The device refuses a blank network while one is saved (SQMeter spec 011 FR-009).
  ssid: z.string().refine((v) => v.trim() !== '', t('validation.configSchema.wifiSsidIsRequired')),
  password: z.string(),
  hostname: z
    .string()
    .min(1, t('validation.configSchema.hostnameIsRequired'))
    .max(32, t('validation.configSchema.hostnameCanBeAtMost'))
    .regex(/^[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?$/, t('validation.configSchema.useLettersNumbersAndHyphens')),
  mdns: z.boolean().optional(),
  ipv6: z.boolean().optional(),
  autoReconnect: z.boolean(),
  reconnectDelayMs: z.number().int().positive().max(86400000, t('validation.configSchema.reconnectDelayCanBeAtMost24')),
  maxReconnectDelayMs: z.number().int().positive().max(86400000, t('validation.configSchema.reconnectDelayCanBeAtMost24')),
});

const MQTT_TOPIC = /^[a-zA-Z0-9_-]+(\/[a-zA-Z0-9_-]+)*$/;

export const mqttConfigSchema = z
  .object({
    enabled: z.boolean(),
    broker: z.string(),
    port: z.number().int().min(1, t('validation.configSchema.portMustBeAtLeast')).max(65535, t('validation.configSchema.portMustBeAtMost')),
    topic: z.string(),
    username: z.string(),
    password: z.string(),
    publishIntervalMs: z
      .number()
      .int()
      .min(1000, t('validation.configSchema.publishIntervalMustBeAt'))
      .max(86400000, t('validation.configSchema.publishIntervalCanBeAt')),
    homeAssistant: z.object({ enabled: z.boolean(), discoveryPrefix: z.string() }).optional(),
  })
  // D-33 (a constraint): the device's own message.
  .refine((data) => !data.enabled || data.broker.trim().length > 0, {
    message: t('validation.configSchema.mqttBrokerAndTopicAre'),
    path: ['broker'],
  })
  // Host forms, IPv6 included - the device's rules (lib/NetAddress, SQMeter spec 015).
  .superRefine((data, ctx) => {
    if (data.broker === '') return;
    const host = parseHost(data.broker);
    if (host.error) ctx.addIssue({ code: 'custom', path: ['broker'], message: host.error });
  })
  .refine((data) => !data.enabled || data.topic.trim().length > 0, {
    message: t('validation.configSchema.mqttBrokerAndTopicAre'),
    path: ['topic'],
  })
  // Same rule as the device: letters, digits, _ - and / between levels.
  .refine((data) => !data.enabled || MQTT_TOPIC.test(data.topic), {
    message: t('validation.configSchema.useLettersNumbersAndWith'),
    path: ['topic'],
  })
  .refine((data) => !data.homeAssistant?.enabled || MQTT_TOPIC.test(data.homeAssistant.discoveryPrefix), {
    message: t('validation.configSchema.useLettersNumbersAndWith'),
    path: ['homeAssistant', 'discoveryPrefix'],
  });

// Command-line uploads without a password are kept but not in effect - a
// dependency (D-32), shown under the switch, not a rejected save.
export const otaConfigSchema = z.object({
  enabled: z.boolean(),
  password: z.string(),
});

export const authConfigSchema = z
  .object({
    enabled: z.boolean(),
    username: z.string(),
    password: z.string(),
  })
  // D-34 (a constraint): the device's own rule and message.
  .refine((data) => !data.enabled || data.password.length > 0, {
    message: t('validation.configSchema.httpAuthPasswordIsRequired'),
    path: ['password'],
  });

export const ntpConfigSchema = z.object({
  enabled: z.boolean(),
  server1: z
    .string()
    .min(1, t('validation.configSchema.primaryNtpServerIsRequired'))
    .refine((v) => !isIpv6Literal(v), ntpIpv6Text()),
  server2: z.string().refine((v) => !isIpv6Literal(v), ntpIpv6Text()),
  timezone: z.string().min(1, t('validation.configSchema.timezoneIsRequired')),
  syncIntervalMs: z
    .number()
    .int()
    .min(600000, t('validation.configSchema.syncIntervalMustBeAt'))
    .max(86400000, t('validation.configSchema.syncIntervalCanBeAt')),
});

export const alpacaConfigSchema = z.object({
  enabled: z.boolean(),
});

export const configSchema = z.object({
  deviceName: z.string().min(1, t('validation.configSchema.deviceNameIsRequired')),
  wifi: wifiConfigSchema,
  mqtt: mqttConfigSchema,
  ota: otaConfigSchema,
  auth: authConfigSchema,
  ntp: ntpConfigSchema,
  alpaca: alpacaConfigSchema.optional(),
  alerts: alertsConfigSchema.optional(),
});

export type ConfigSchema = z.infer<typeof configSchema>;
export type ValidationErrors = Record<string, string>;

export const getConfigValidationErrors = (candidate: unknown): ValidationErrors => {
  const result = configSchema.safeParse(candidate);
  if (result.success) return {};

  return Object.fromEntries(result.error.issues.map((issue) => [issue.path.join('.') || 'config', issue.message]));
};

export const hasConfigValidationErrors = (errors: ValidationErrors): boolean => Object.keys(errors).length > 0;

export const getConfigValidationMessage = (errors: ValidationErrors): string => {
  const messages = Object.values(errors);
  if (messages.length === 1) return t('validation.configSchema.pleaseFix1ValidationError', { value: messages[0] });
  return t('validation.configSchema.pleaseFixLengthValidationErrors', { length: messages.length });
};
