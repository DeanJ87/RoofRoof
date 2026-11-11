import { z } from 'zod';
import { t } from '../i18n';

// Roof settings (stored under "roof"). The ranges and the two cross rules are
// the device's (lib/ConfigModel/src/ConfigValidate.cpp, test/fixtures/config-ranges.json).

const MIN_HALL_GAP_COUNTS = 80;

export const roofConfigSchema = z
  .object({
    travelMm: z.number().int().min(100).max(20000),
    speedMmPerSecond: z.number().min(1).max(500),
    timeoutMarginSeconds: z.number().int().min(3).max(300),
    leaveMagnetSeconds: z.number().int().min(2).max(120),
    rampUpMs: z.number().int().min(0).max(30000),
    rampDownMs: z.number().int().min(0).max(30000),
    topDutyPercent: z.number().int().min(20).max(100),
    slowDutyPercent: z.number().int().min(10).max(100),
    openDirection: z.enum(['forward', 'reverse']),
    hallHighEnd: z.enum(['open', 'closed']),
    hallHighCounts: z.number().int().min(100).max(3995),
    hallLowCounts: z.number().int().min(100).max(3995),
    currentSenseEnabled: z.boolean(),
    maxCurrentAmps: z.number().min(0.5).max(40),
    currentAmpsPerVolt: z.number().min(0.5).max(100),
    minRunningAmps: z.number().min(0).max(20),
    minSupplyVolts: z.number().min(6).max(15),
  })
  .superRefine((data, ctx) => {
    if (data.slowDutyPercent > data.topDutyPercent) {
      ctx.addIssue({
        code: 'custom',
        path: ['slowDutyPercent'],
        message: t('validation.roofSchema.slowDutyCantBeAbove'),
      });
    }
    if (data.hallHighCounts < data.hallLowCounts + MIN_HALL_GAP_COUNTS) {
      ctx.addIssue({
        code: 'custom',
        path: ['hallHighCounts'],
        message: t('validation.roofSchema.hallHighMustBeAbove'),
      });
    }
  });
