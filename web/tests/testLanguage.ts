import type { Page } from '@playwright/test';
import { readFileSync } from 'fs';
import { resolve } from 'path';

// RoofRoof ships English only (spec 001 research R10), but the language
// framework stays, so a language is one file later. These tests prove the parts
// a language changes - number formats in the UI - and the parts it must never
// change - the API (I18N-06) - with a test-only pack: English's own text under
// another language's code, so the locale is the only difference.

export const english: Record<string, string | Record<string, string>> = JSON.parse(
  readFileSync(resolve(import.meta.dirname, '../src/i18n/en.json'), 'utf8'),
);

/** A test-only pack for `code`: English text, `code`'s number and date formats. */
export const testPack = (code: string) => ({ code, messages: english });

/**
 * Installs the pack before the page's scripts run (web/src/i18n/index.ts reads
 * it when it loads). English needs nothing: it is built in.
 */
export const installTestLanguage = async (page: Page, code: string) => {
  if (code === 'en') return;
  await page.addInitScript((pack) => {
    (globalThis as { __i18nTestLanguage?: unknown }).__i18nTestLanguage = pack;
  }, testPack(code));
};
