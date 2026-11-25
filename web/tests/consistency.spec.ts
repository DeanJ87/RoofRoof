import { test, expect, type Page } from '@playwright/test';

// Spec 028: "?" tips above every card (FR-011), one size for secondary text
// (FR-009) and a note kept with its reading row (FR-010).

const statusCard = (page: Page) => page.locator('[data-inventory="status-card"]');

const open = async (page: Page) => {
  await page.goto('./#/');
  await expect(statusCard(page)).toBeVisible({ timeout: 15_000 });
};

test('the Status card tip is in the top layer, over the next card and inside the window', async ({ page }) => {
  await open(page);
  const tip = statusCard(page).locator('.card-title .info-tip');
  await tip.hover();
  const body = tip.locator('.info-tip-body');
  await expect(body).toBeVisible();
  const placed = await body.evaluate((tip) => {
    const box = tip.getBoundingClientRect();
    return {
      layered: tip.matches(':popover-open'),
      inside: box.left >= 0 && box.top >= 0 && box.right <= innerWidth && box.bottom <= innerHeight,
    };
  });
  expect(placed).toEqual({ layered: true, inside: true });
  await page.mouse.move(0, 0);
  await expect(body).toBeHidden();
});

test('a tip near the right edge on a phone stays inside the window', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 760 });
  await open(page);
  const tip = page.locator('.card-title .info-tip').last();
  await tip.scrollIntoViewIfNeeded();
  await tip.click();
  const box = await tip.locator('.info-tip-body').evaluate((body) => {
    const r = body.getBoundingClientRect();
    return { left: r.left, right: r.right };
  });
  expect(box.left).toBeGreaterThanOrEqual(0);
  expect(box.right).toBeLessThanOrEqual(360);
});

test('secondary text has one size everywhere it shows on the dashboard and in Settings', async ({ page }) => {
  await open(page);
  const sizes = async () =>
    page.evaluate(() =>
      Array.from(document.querySelectorAll('.note, .metric-sub, .subtext, .choice-detail')).map((el) => getComputedStyle(el).fontSize),
    );
  const dashboard = await sizes();
  await page.goto('./#/settings?tab=roof');
  await expect(page.getByRole('heading', { name: 'Hall sensor' })).toBeVisible({ timeout: 15_000 });
  const settings = await sizes();
  const all = [...dashboard, ...settings];
  expect(all.length).toBeGreaterThan(3);
  expect(new Set(all).size).toBe(1);
});

test('a note under a reading row sits with that row', async ({ page }) => {
  await open(page);
  const rows = await page.evaluate(() =>
    Array.from(document.querySelectorAll('.reading-row + .note')).map((note) => {
      const row = note.previousElementSibling as HTMLElement;
      return getComputedStyle(row).borderBottomWidth;
    }),
  );
  expect(rows.length).toBeGreaterThan(0);
  for (const width of rows) expect(width).toBe('0px');
});
