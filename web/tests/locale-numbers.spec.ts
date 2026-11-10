import { test, expect, type Page } from '@playwright/test';
import { installTestLanguage, testPack } from './testLanguage';

// Numbers follow the active language (specs/023-i18n FR-017): typed decimals
// use the language's separator and reach the device exactly; shown values use
// it too, with Latin digits in every language. RoofRoof ships English only,
// so the test installs a pack of its own (spec 001 research R10).

const open = async (page: Page, code: string, route: string) => {
  await installTestLanguage(page, code);
  await page.goto('about:blank');
  await page.goto(`./${route}`);
  await expect(page.locator('html')).toHaveAttribute('lang', code);
  await expect(page.locator('main')).toBeVisible();
  await page.waitForTimeout(1500); // first ticks of the emulated device
};

const config = (page: Page) => page.evaluate(async () => (await fetch('/api/config')).json());
const text = (key: string) => String(testPack('de').messages[key]);

// "Silent for" takes minutes in half-minute steps; the device stores seconds.
const silentFor = (page: Page) => page.getByRole('textbox', { name: text('settings.alerts.silentFor'), exact: true });

test('German decimals typed into a setting reach the device', async ({ page }) => {
  await open(page, 'de', '#/settings?tab=alerts');
  const field = silentFor(page);
  await expect(field).toHaveValue('2');
  await field.fill('2,5');
  await field.press('Tab');
  await page.getByRole('button', { name: text('settings.save') }).click();
  await expect.poll(async () => (await config(page)).alerts.clientSilentSeconds).toBe(150);
  await expect(field).toHaveValue('2,5');
});

test('an unclear number is kept with a message, not truncated', async ({ page }) => {
  await open(page, 'de', '#/settings?tab=alerts');
  const field = silentFor(page);
  await field.fill('1.234');
  await field.press('Tab');
  await expect(field).toHaveValue('1.234');
  await expect(field).toHaveAttribute('aria-invalid', 'true');
  expect((await config(page)).alerts.clientSilentSeconds).toBe(120);
});

for (const code of ['fr', 'ar']) {
  test(`${code}: System and Alpaca pages show numbers the language's way`, async ({ page }, testInfo) => {
    for (const route of ['#/system', '#/alpaca']) {
      await open(page, code, route);
      const shown = await page.locator('main').innerText();
      expect(shown, 'Latin digits only').not.toMatch(/[٠-٩۰-۹]/);
      if (code === 'fr' && route === '#/system') expect(shown).toMatch(/\d+,\d{2} (KB|MB)/);
      if (code === 'ar' && route === '#/system') expect(shown).toMatch(/\d+\.\d{2} (KB|MB)/);
      await testInfo.attach(`${code}${route.replace('#/', '-')}.png`, {
        body: await page.screenshot({ fullPage: true }),
        contentType: 'image/png',
      });
    }
  });
}
