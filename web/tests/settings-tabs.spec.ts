import { test, expect } from '@playwright/test';

// Settings follows the address (026 audit A23): a link, Back/Forward or a
// pasted address can change the tab while the page stays open.

test('Settings switches tab when the address changes, and with Back', async ({ page }) => {
  await page.goto('./#/settings?tab=device');
  const selected = page.locator('[role="tab"][aria-selected="true"]');
  await expect(selected).toHaveText('Device', { timeout: 15_000 });
  await page.evaluate(() => (window.location.hash = '#/settings?tab=network'));
  await expect(selected).toHaveText('Network');
  await page.evaluate(() => (window.location.hash = '#/settings?tab=alerts'));
  await expect(selected).toHaveText('Alerts');
  await page.goBack();
  await expect(selected).toHaveText('Network');
});

// Spec 013: the Daylight card is on Settings → Roof, reachable by its section link, and saves.
test('Settings → Roof has the Daylight card, and a save reaches the device', async ({ page }) => {
  await page.goto('./#/settings?tab=roof&section=daylight');
  const card = page.locator('#daylight');
  await expect(card).toBeVisible({ timeout: 15_000 });
  await expect(card.locator('[data-field="daylight.mode"]')).toBeVisible();
  await card.locator('[data-field="daylight.mode"]').selectOption('close');
  // The save bar covers the last card, so the switch is clicked without a pointer.
  await card.getByRole('switch', { name: 'Location set' }).evaluate((el: HTMLElement) => el.click());
  await card.locator('[data-field="location.latitude"]').fill('51.5');
  await card.locator('[data-field="location.longitude"]').fill('-0.1');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() =>
      page.evaluate(async () => {
        const config = await (await fetch('/api/config')).json();
        return `${config.daylight.mode}:${config.location.set}:${config.location.latitude}`;
      }),
    )
    .toBe('close:true:51.5');
});
