import { z } from 'zod';
import { t } from '../i18n';
import { parseHttpUrl } from '../lib/netAddress';

// Alerts settings (split from configSchema.ts to keep files short).

type EventSettings = Record<string, { level: number; sound: string } | undefined>;

// The roof's critical alerts can't be switched off, quietened or silenced (spec 007).
const checkCriticalEvents = (events: EventSettings, ctx: z.RefinementCtx) => {
  for (const key of ['mount_not_parked', 'roof_not_closed']) {
    const event = events[key];
    if (event && event.level < 3) {
      ctx.addIssue({ code: 'custom', path: ['events', key, 'level'], message: t('validation.configSchema.roofCriticalAlertsAreUrgent') });
    }
    if (event && (event.sound === 'none' || event.sound === 'vibrate')) {
      ctx.addIssue({ code: 'custom', path: ['events', key, 'sound'], message: t('validation.configSchema.roofCriticalAlertsMakeASound') });
    }
  }
};

export const alertsConfigSchema = z
  .object({
    enabled: z.boolean(),
    events: z.record(
      z.string(),
      z.object({
        level: z.number().int().min(0).max(4),
        sound: z
          .string()
          .max(32)
          .regex(/^[a-z0-9_-]*$/i, t('validation.configSchema.notAPushoverSoundName')),
        title: z.string().max(80, t('validation.configSchema.titlesAreUpTo80')).optional(),
        message: z.string().max(240, t('validation.configSchema.messagesAreUpTo240')).optional(),
      }),
    ),
    sendMode: z.enum(['any', 'whileConnected']),
    clientSilentSeconds: z
      .number()
      .int()
      .min(30, t('validation.configSchema.atLeast30Seconds'))
      .max(3600, t('validation.configSchema.atMost60Minutes')),
    cooldownSeconds: z.number().int().min(0).max(86400, t('validation.configSchema.mustBeAtMost24')),
    pushover: z.object({
      enabled: z.boolean(),
      userKey: z.string(),
      appToken: z.string(),
      sound: z.string(),
    }),
    ntfy: z.object({ enabled: z.boolean(), server: z.string(), topic: z.string(), token: z.string() }),
    webhook: z.object({ enabled: z.boolean(), url: z.string(), authHeader: z.string(), insecureTls: z.boolean() }),
    mqtt: z.object({ enabled: z.boolean() }),
  })
  .superRefine((data, ctx) => {
    checkCriticalEvents(data.events, ctx);
    // Pushover keys are exactly 30 letters/digits; "********" is the stored, masked value.
    const pushoverKey = /^([A-Za-z0-9]{30}|\*{8})$/;
    if (data.pushover.enabled && !pushoverKey.test(data.pushover.userKey.trim())) {
      ctx.addIssue({
        code: 'custom',
        path: ['pushover', 'userKey'],
        message: t('validation.configSchema.theUserKeyIsThe'),
      });
    }
    if (data.pushover.enabled && !pushoverKey.test(data.pushover.appToken.trim())) {
      ctx.addIssue({
        code: 'custom',
        path: ['pushover', 'appToken'],
        message: t('validation.configSchema.theAppTokenIsThe'),
      });
    }
    if (data.ntfy.enabled) {
      const server = parseHttpUrl(data.ntfy.server);
      if (server.error) ctx.addIssue({ code: 'custom', path: ['ntfy', 'server'], message: server.error });
      if (!data.ntfy.topic) {
        ctx.addIssue({ code: 'custom', path: ['ntfy', 'topic'], message: t('validation.configSchema.topicIsRequired') });
      }
    }
    if (data.webhook.enabled) {
      const url = parseHttpUrl(data.webhook.url);
      if (url.error) ctx.addIssue({ code: 'custom', path: ['webhook', 'url'], message: url.error });
    }
  });
