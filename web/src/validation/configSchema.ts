import { z } from 'zod';
import { t } from '../i18n';
import { alertsConfigSchema } from './alertsSchema';
import { roofConfigSchema } from './roofSchema';
import { isIpv6Literal, ntpIpv6Text, parseHost, parseHttpUrl } from '../lib/netAddress';
import { sqmeterAddressError } from '../lib/sqmeterAddress';

// A blank network is allowed (a new device, a board on Ethernet); only blanking a saved one is
// refused, as the device does (getConfigValidationErrors, which knows the saved settings).
export const wifiConfigSchema = z.object({
  ssid: z.string(),
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
    // null clears the saved password (spec 020: a broker copied from SQMeter).
    password: z.string().nullable(),
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
    telemetry: z
      .object({
        enabled: z.boolean(),
        restVoltsSeconds: z
          .number()
          .int()
          .min(0, t('validation.configSchema.telemetryIntervalMustBeBetween'))
          .max(3600, t('validation.configSchema.telemetryIntervalMustBeBetween')),
      })
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
// The device's length limits are in UTF-8 bytes.
const utf8Bytes = (value: string) => new TextEncoder().encode(value).length;

export const safetyConfigSchema = z
  .object({
    http: z.object({
      enabled: z.boolean(),
      url: z.string(),
      // Spec 020: SQMeter's login; the device's limits and messages.
      // The device counts UTF-8 bytes (at most 64) and refuses bytes below 0x20 in the user name.
      username: z
        .string()
        .refine((v) => utf8Bytes(v) <= 64, t('validation.configSchema.sqmeterLoginTooLong'))
        .refine((v) => !v.includes(':'), t('validation.configSchema.sqmeterUserNoColon'))
        // eslint-disable-next-line no-control-regex -- the device refuses control characters (spec 020)
        .refine((v) => !/[\u0000-\u001f]/.test(v), t('validation.configSchema.sqmeterUserNoControl'))
        .optional(),
      password: z
        .string()
        .refine((v) => utf8Bytes(v) <= 64, t('validation.configSchema.sqmeterLoginTooLong'))
        .nullable()
        .optional(),
    }),
    mqtt: z.object({ enabled: z.boolean(), topic: z.string() }),
    staleSeconds: z
      .number()
      .int()
      .min(10, t('validation.configSchema.staleTimeMustBeBetween'))
      .max(600, t('validation.configSchema.staleTimeMustBeBetween')),
  })
  // Spec 020 FR-036, FR-039: SQMeter's address only (http or https), checked when set, as the device does.
  .superRefine((data, ctx) => {
    const error =
      data.http.enabled && data.http.url.trim() === ''
        ? t('validation.configSchema.sqmeterAddressNotSet')
        : sqmeterAddressError(data.http.url);
    if (error) ctx.addIssue({ code: 'custom', path: ['http', 'url'], message: error });
  })
  .refine((data) => !data.mqtt.enabled || MQTT_TOPIC.test(data.mqtt.topic), {
    message: t('validation.configSchema.useLettersNumbersAndWith'),
    path: ['mqtt', 'topic'],
  });

export const parkConfigSchema = z
  .object({
    nina: z.object({
      url: z.string().superRefine((value, ctx) => {
        if (value === '') return;
        const url = parseHttpUrl(value);
        if (url.error) ctx.addIssue({ code: 'custom', message: url.error });
        // Spec 027: http or https (parseHttpUrl refuses any other scheme).
      }),
      api: z.literal('v2'),
    }),
    // Spec 024: optional, as older firmware doesn't send it (the device derives it from the switches).
    source: z.enum(['nina', 'switches', 'ninaAndSwitches']).optional(),
    switches: z.enum(['none', 'one', 'two']),
    requestPark: z.boolean().optional(),
    // Spec 005: how long the close sequence waits for the park to be confirmed.
    timeoutSeconds: z
      .number()
      .int()
      .min(30, t('validation.configSchema.parkTimeLimitMustBeBetween'))
      .max(1800, t('validation.configSchema.parkTimeLimitMustBeBetween')),
  })
  // Spec 024 FR-002: N.I.N.A. only fits no switch; a switch source one or two.
  .superRefine((data, ctx) => {
    if (!data.source) return;
    if (data.source === 'nina' && data.switches !== 'none')
      ctx.addIssue({ code: 'custom', path: ['switches'], message: t('validation.configSchema.parkNinaNoSwitch') });
    if (data.source !== 'nina' && data.switches === 'none')
      ctx.addIssue({ code: 'custom', path: ['switches'], message: t('validation.configSchema.parkSwitchesNeeded') });
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
  // Spec 030: unsafe time before the automatic close starts (0: none).
  unsafeHoldSeconds: minutesOrSeconds(0, 300, t('validation.configSchema.unsafeHoldMustBeBetween')),
  // Spec 030: close without N.I.N.A.'s park once it has been lost this long (off by default).
  ninaLostClose: z.boolean(),
  ninaLostMinutes: minutesOrSeconds(1, 60, t('validation.configSchema.ninaLostMustBeBetween')),
  // Spec 044: a person's Stop of the automatic close lapses never (default), in rain or always, after stopHoldMinutes.
  personStopLapse: z.enum(['never', 'rain', 'always']),
  stopHoldMinutes: minutesOrSeconds(2, 60, t('validation.configSchema.stopHoldMustBeBetween')),
});

export const uiConfigSchema = z.object({ confirmOpenClose: z.boolean() });

// Spec 017: advanced diagnostics.
export const diagConfigSchema = z.object({ enabled: z.boolean(), keepSummaries: z.boolean(), mqtt: z.boolean() });

// Spec 022: the test box. `until` is the device's and never checked here.
export const testBoxConfigSchema = z.object({
  park: z.boolean(),
  weather: z.boolean(),
  daylight: z.boolean(),
  hours: z
    .number()
    .int()
    .min(1, t('validation.configSchema.testBoxHoursMustBeBetween'))
    .max(720, t('validation.configSchema.testBoxHoursMustBeBetween')),
  until: z.number().optional(),
});

export const locationConfigSchema = z.object({
  // Spec 020: optional, as older firmware doesn't send it; `sqmeter` is the device's and never checked here.
  source: z.enum(['sqmeter', 'manual'], t('validation.configSchema.locationSourceMustBe')).optional(),
  sqmeter: z.object({ gps: z.boolean(), savedAt: z.number() }).optional(),
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

// Spec 020: which groups follow SQMeter; `syncedAt` is the device's.
const syncGroupSchema = z.object({ follow: z.boolean(), syncedAt: z.number() });
export const sqmeterSyncSchema = z.object({ time: syncGroupSchema, mqtt: syncGroupSchema, alerts: syncGroupSchema });

// Spec 015: the setup wizard's state and page.
export const setupConfigSchema = z.object({
  state: z.enum(['new', 'offered', 'done'], t('validation.configSchema.setupStateMustBe')).optional(),
  step: z
    .number()
    .int()
    .min(0, t('validation.configSchema.setupStepMustBe'))
    .max(11, t('validation.configSchema.setupStepMustBe'))
    .optional(),
});

// Spec 023: the board and each function's GPIO; whether a set is allowed is the device's call
// (POST /api/pins/validate), as is a named preset's consistency.
const gpio = z
  .number()
  .int(t('validation.configSchema.gpioMustBeBetween'))
  .min(0, t('validation.configSchema.gpioMustBeBetween'))
  .max(255, t('validation.configSchema.gpioMustBeBetween'));
export const pinsConfigSchema = z.object({
  preset: z.enum(['devkit', 'wt32-carrier-v1', 'custom', 'unset']),
  board: z.enum(['esp32-devkit', 'wt32-eth01']),
  rpwm: gpio,
  lpwm: gpio,
  enable: gpio,
  currentR: gpio,
  currentL: gpio,
  supply: gpio,
  hall: gpio,
  parkA: gpio,
  parkB: gpio,
  parkAPullup: z.enum(['internal', 'external']).optional(),
  parkBPullup: z.enum(['internal', 'external']).optional(),
});

// Spec 023 Advanced: the analog front end, with the device's ranges (lib/BoardLogic Analog.h).
// Whether a named preset's values match it, and the 3.3 V warnings, are the device's call.
const ohms = z
  .number()
  .int(t('validation.configSchema.resistorMustBeBetween'))
  .min(100, t('validation.configSchema.resistorMustBeBetween'))
  .max(10000000, t('validation.configSchema.resistorMustBeBetween'));
const divider = { top: ohms, bottom: ohms };
const gain = z.number().min(0.8, t('validation.configSchema.gainMustBeBetween')).max(1.25, t('validation.configSchema.gainMustBeBetween'));
const offset = (most: number, message: string) => z.number().min(-most, message).max(most, message);
const currentTrim = z.object({
  ...divider,
  gain,
  offsetAmps: offset(0.5, t('validation.configSchema.currentOffsetWithin')),
});
export const analogConfigSchema = z.object({
  preset: z.enum(['devkit-bench', 'wt32-carrier-v1.1', 'custom']),
  supply: z.object({ ...divider, gain, offsetVolts: offset(1, t('validation.configSchema.supplyOffsetWithin')) }),
  hall: z.object(divider),
  currentR: currentTrim,
  currentL: currentTrim,
});
export const networkConfigSchema = z.object({ mode: z.enum(['auto', 'ethernet', 'wifi']) });

const configShape = z.object({
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
  sqmeterSync: sqmeterSyncSchema.optional(),
  autoClose: autoCloseConfigSchema.optional(),
  ui: uiConfigSchema.optional(),
  diag: diagConfigSchema.optional(),
  testBox: testBoxConfigSchema.optional(),
  alerts: alertsConfigSchema.optional(),
  setup: setupConfigSchema.optional(),
  pins: pinsConfigSchema.optional(),
  network: networkConfigSchema.optional(),
  analog: analogConfigSchema.optional(),
});

// Spec 024 FR-002: the test box's park bypass stands in for N.I.N.A., which park switches only don't use.
export const configSchema = configShape.superRefine((data, ctx) => {
  if (data.testBox?.park && data.park?.source === 'switches')
    ctx.addIssue({ code: 'custom', path: ['testBox', 'park'], message: t('validation.configSchema.testBoxParkSwitchesOnly') });
});

export type ConfigSchema = z.infer<typeof configSchema>;
export type ValidationErrors = Record<string, string>;

type SavedWifi = { wifi?: { ssid?: unknown } } | null | undefined;
const ssidOf = (value: unknown): string => {
  const ssid = (value as SavedWifi)?.wifi?.ssid;
  return typeof ssid === 'string' ? ssid.trim() : '';
};

/**
 * The settings' validation errors. With `saved` (the settings as loaded), a saved WiFi network
 * can't be blanked: the device refuses that ("use WiFi setup to change networks", SQMeter spec
 * 011 FR-009) but never requires a network (a new device, a board on Ethernet: spec 023).
 */
export const getConfigValidationErrors = (candidate: unknown, saved?: unknown): ValidationErrors => {
  const result = configSchema.safeParse(candidate);
  const errors: ValidationErrors = result.success
    ? {}
    : Object.fromEntries(result.error.issues.map((issue) => [issue.path.join('.') || 'config', issue.message]));
  if (ssidOf(saved) !== '' && ssidOf(candidate) === '' && !errors['wifi.ssid'])
    errors['wifi.ssid'] = t('validation.configSchema.wifiCantBeBlank');
  return errors;
};

export const hasConfigValidationErrors = (errors: ValidationErrors): boolean => Object.keys(errors).length > 0;

export const getConfigValidationMessage = (errors: ValidationErrors): string => {
  const messages = Object.values(errors);
  if (messages.length === 1) return t('validation.configSchema.pleaseFix1ValidationError', { value: messages[0] });
  return t('validation.configSchema.pleaseFixLengthValidationErrors', { length: messages.length });
};
