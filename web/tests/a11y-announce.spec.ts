import { test, expect, type Page } from '@playwright/test';

// Live status don't flood screen readers (SQMeter spec 022 SC-003, FR-009): the
// polite live region stays silent while values update every second.

const recordAnnouncements = (page: Page) =>
  page.evaluate(() => {
    const region = document.getElementById('roofroof-live')!;
    const heard: string[] = [];
    (window as unknown as { heard: string[] }).heard = heard;
    new MutationObserver(() => {
      const text = region.textContent?.trim();
      if (text) heard.push(text);
    }).observe(region, { childList: true, characterData: true, subtree: true });
  });

const heard = (page: Page) => page.evaluate(() => (window as unknown as { heard: string[] }).heard);

test('the dashboard is silent while the status updates', async ({ page }) => {
  test.setTimeout(90_000);
  await page.goto('./#/');
  await expect(page.locator('main')).toBeVisible();
  await page.waitForTimeout(2500);
  await recordAnnouncements(page);

  // Readings tick every second; nothing should be said.
  await page.waitForTimeout(8000);
  expect(await heard(page)).toEqual([]);
});
