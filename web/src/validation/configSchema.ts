import { z } from 'zod';
import { t } from '../i18n';
import { alertsConfigSchema } from './alertsSchema';
import { roofConfigSchema } from './roofSchema';
import { isIpv6Literal, ntpIpv6Text, parseHost, parseHttpUrl } from '../lib/netAddress';

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
    roofCommands: z.boolean().optional(),
    changeMinSeconds: z
      .number()
      .int()
      .min(1, t('validation.configSchema.changeGapMustBeBetween'))
      .max(60, t('validation.configSchema.changeGapMustBeBetween'))
      .optional(),
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

// SQMeter and N.I.N.A. serve plain http only; the device's rules and messages
// (lib/ConfigModel ConfigValidate.cpp, spec 004 contracts/settings.md).
const isPlainHttp = (url: string) => url.startsWith('http://');

export const safetyConfigSchema = z
  .object({
    http: z.object({ enabled: z.boolean(), url: z.string() }),
    mqtt: z.object({ enabled: z.boolean(), topic: z.string() }),
    staleSeconds: z
      .number()
      .int()
      .min(10, t('validation.configSchema.staleTimeMustBeBetween'))
      .max(600, t('validation.configSchema.staleTimeMustBeBetween')),
    rainContact: z.boolean(),
  })
  .superRefine((data, ctx) => {
    if (!data.http.enabled) return;
    const url = parseHttpUrl(data.http.url);
    if (url.error) ctx.addIssue({ code: 'custom', path: ['http', 'url'], message: url.error });
    else if (!isPlainHttp(data.http.url))
      ctx.addIssue({ code: 'custom', path: ['http', 'url'], message: t('validation.configSchema.sqmeterUrlMustStartWithHttp') });
  })
  .refine((data) => !data.mqtt.enabled || MQTT_TOPIC.test(data.mqtt.topic), {
    message: t('validation.configSchema.useLettersNumbersAndWith'),
    path: ['mqtt', 'topic'],
  });

export const parkConfigSchema = z.object({
  nina: z.object({
    url: z.string().superRefine((value, ctx) => {
      if (value === '') return;
      const url = parseHttpUrl(value);
      if (url.error) ctx.addIssue({ code: 'custom', message: url.error });
      else if (!isPlainHttp(value)) ctx.addIssue({ code: 'custom', message: t('validation.configSchema.ninaUrlMustStartWithHttp') });
    }),
    api: z.literal('v2'),
  }),
  switches: z.enum(['none', 'one', 'two']),
  // Spec 005: how long the close sequence waits for the park to be confirmed.
  timeoutSeconds: z
    .number()
    .int()
    .min(30, t('validation.configSchema.parkTimeLimitMustBeBetween'))
    .max(1800, t('validation.configSchema.parkTimeLimitMustBeBetween')),
});

// Spec 013: the daylight failsafe and the roof's location.
export const daylightConfigSchema = z.object({
  mode: z.enum(['off', 'alert', 'close'], t('validation.configSchema.daylightModeMustBeOff')),
  sunAltitudeDeg: z
    .number()
    .min(-18, t('validation.configSchema.sunAltitudeMustBeBetween'))
    .max(30, t('validation.configSchema.sunAltitudeMustBeBetween')),
  bandDeg: z
    .number()
    .min(0.1, t('validation.configSchema.bandMustBeBetween'))
    .max(2, t('validation.configSchema.bandMustBeBetween'))
    .optional(),
});

// Spec 014: the automatic close's policy. The ranges are the device's
// (lib/ConfigModel, lib/CloseSequenceLogic/ClosePolicy.h, the shared range fixture).
const minutesOrSeconds = (min: number, max: number, message: string) => z.number().int().min(min, message).max(max, message);
export const autoCloseConfigSchema = z.object({
  rearmMinutes: minutesOrSeconds(5, 60, t('validation.configSchema.rearmMustBeBetween')),
  parkRequestSeconds: minutesOrSeconds(10, 120, t('validation.configSchema.parkRequestMustBeBetween')),
  parkHoldSeconds: minutesOrSeconds(4, 30, t('validation.configSchema.parkHoldMustBeBetween')),
  retryMinutes: minutesOrSeconds(1, 30, t('validation.configSchema.backOffMustBeBetween')),
  closeStartSeconds: minutesOrSeconds(25, 120, t('validation.configSchema.closeStartMustBeBetween')),
  closeGuardSeconds: minutesOrSeconds(10, 120, t('validation.configSchema.closeGuardMustBeBetween')),
  notStarted: z.enum(['halt', 'retry']),
  remindMinutes: minutesOrSeconds(10, 120, t('validation.configSchema.reminderMustBeBetween')),
  remoteStop: z.boolean(),
});

export const uiConfigSchema = z.object({ confirmOpenClose: z.boolean() });

// Spec 017: advanced diagnostics.
export const diagConfigSchema = z.object({ enabled: z.boolean(), keepSummaries: z.boolean(), mqtt: z.boolean() });

export const locationConfigSchema = z.object({
  set: z.boolean(),
  latitude: z
    .number()
    .min(-90, t('validation.configSchema.latitudeMustBeBetween'))
    .max(90, t('validation.configSchema.latitudeMustBeBetween')),
  longitude: z
    .number()
    .min(-180, t('validation.configSchema.longitudeMustBeBetween'))
    .max(180, t('validation.configSchema.longitudeMustBeBetween')),
});

// Spec 015: the setup wizard's state and page.
export const setupConfigSchema = z.object({
  state: z.enum(['new', 'offered', 'done'], t('validation.configSchema.setupStateMustBe')).optional(),
  step: z
    .number()
    .int()
    .min(0, t('validation.configSchema.setupStepMustBe'))
    .max(10, t('validation.configSchema.setupStepMustBe'))
    .optional(),
});

export const configSchema = z.object({
  deviceName: z.string().min(1, t('validation.configSchema.deviceNameIsRequired')),
  wifi: wifiConfigSchema,
  mqtt: mqttConfigSchema,
  ota: otaConfigSchema,
  auth: authConfigSchema,
  ntp: ntpConfigSchema,
  alpaca: alpacaConfigSchema.optional(),
  safety: safetyConfigSchema.optional(),
  park: parkConfigSchema.optional(),
  roof: roofConfigSchema.optional(),
  daylight: daylightConfigSchema.optional(),
  location: locationConfigSchema.optional(),
  autoClose: autoCloseConfigSchema.optional(),
  ui: uiConfigSchema.optional(),
  diag: diagConfigSchema.optional(),
  alerts: alertsConfigSchema.optional(),
  setup: setupConfigSchema.optional(),
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
