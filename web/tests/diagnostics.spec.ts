import { test, expect, type Page } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';
import { readFileSync } from 'fs';

// Advanced diagnostics in the demo (spec 017 US6): switched on in Settings,
// the Diagnostics page follows a move as it happens, lists it when it ends and
// offers its profile as CSV, and passes the accessibility checks on a phone.

const TAGS = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];

const ready = async (page: Page, route: string) => {
  await page.goto(`./${route}`);
  await expect(page.locator('main')).toBeVisible();
  await page.waitForTimeout(2500); // first ticks of the emulated device
};

const roofCommand = (page: Page, command: string) =>
  page.evaluate(async (name) => (await fetch(`/api/roof/${name}`, { method: 'POST', headers: { 'X-RoofRoof': '1' } })).status, command);

// Segments of the chart's average-current line: one per point after the first.
const chartPoints = (page: Page) =>
  page.evaluate(() => (document.querySelector('.diag-plots .diag-line')?.getAttribute('d') ?? '').split(/[ML]/).length - 1);

const enableInSettings = async (page: Page) => {
  await ready(page, '#/settings?tab=device');
  const card = page.locator('#diagnostics');
  await expect(card.getByRole('switch', { name: 'Keep move summaries' })).toBeDisabled();
  await card.getByRole('switch', { name: 'Advanced diagnostics' }).check();
  await expect(card.getByRole('switch', { name: 'Keep move summaries' })).toBeEnabled();
  await page.getByRole('button', { name: 'Save' }).click();
  await expect(card.getByRole('button', { name: 'Open diagnostics' })).toBeVisible({ timeout: 10_000 });
};

test('off: the page says so, links the setting and opens no stream', async ({ page }) => {
  const sockets: string[] = [];
  page.on('websocket', (ws) => sockets.push(ws.url()));
  await ready(page, '#/diagnostics');
  await expect(page.getByText(/Advanced diagnostics is off/)).toBeVisible();
  expect(sockets.filter((url) => url.includes('/ws/diag'))).toEqual([]);
  await page.getByRole('button', { name: 'Turn on' }).click();
  await expect(page.locator('#diagnostics').getByRole('switch', { name: 'Advanced diagnostics' })).toBeVisible();
});

test('on: a move is charted as it happens, then listed with its CSV', async ({ page }) => {
  test.setTimeout(150_000);
  await enableInSettings(page);
  await page.getByRole('button', { name: 'Open diagnostics' }).click();
  await expect(page.getByRole('heading', { name: 'Control task' })).toBeVisible();

  expect(await roofCommand(page, 'open')).toBe(200);
  await expect(page.getByRole('heading', { name: /^Move \d+ in progress$/ })).toBeVisible({ timeout: 10_000 });
  await expect.poll(() => chartPoints(page), { timeout: 15_000 }).toBeGreaterThan(2);
  const before = await chartPoints(page);
  await expect.poll(() => chartPoints(page), { timeout: 15_000 }).toBeGreaterThan(before);
  await expect(page.getByRole('button', { name: 'Clear log' })).toBeDisabled();
  await expect(page.getByText("The log can't be cleared while the roof moves.")).toBeVisible();

  // The rest of the move at the demo's 10x clock.
  await page.getByRole('button', { name: /Demo/ }).last().click();
  await page.getByRole('button', { name: /^Clock speed 10x/ }).click();
  await page.keyboard.press('Escape');

  const row = page.getByRole('row').filter({ hasText: 'Arrived' }).first();
  await expect(row).toBeVisible({ timeout: 90_000 });
  await row.getByRole('button', { name: /^Show move \d+$/ }).click();
  const csv = page.getByRole('link', { name: 'Download CSV' });
  await expect(csv).toHaveAttribute('href', /^\/api\/diag\/moves\/\d+\?format=csv$/);
  await expect(page.getByRole('link', { name: 'Download JSON' })).toHaveAttribute('href', /^\/api\/diag\/moves\/\d+$/);
  const [download] = await Promise.all([page.waitForEvent('download'), csv.click()]);
  expect(download.suggestedFilename()).toMatch(/^roofroof-move-\d+\.csv$/);
  const text = readFileSync((await download.path())!, 'utf8');
  expect(text.split(/\r?\n/)[0]).toBe('t_ms,amps_avg,amps_max,volts_min,duty_percent');
  await expect(page.getByRole('button', { name: 'Clear log' })).toBeEnabled();
});

test('axe at phone width, with the mode on and a move in progress', async ({ page }) => {
  await enableInSettings(page);
  await page.setViewportSize({ width: 320, height: 640 });
  await ready(page, '#/diagnostics');
  expect(await roofCommand(page, 'open')).toBe(200);
  await expect(page.getByRole('heading', { name: /in progress$/ })).toBeVisible({ timeout: 10_000 });
  await expect.poll(() => chartPoints(page), { timeout: 15_000 }).toBeGreaterThan(2);
  const results = await new AxeBuilder({ page }).withTags(TAGS).analyze();
  expect(results.violations.map((v) => `${v.id}: ${v.nodes.map((n) => n.target.join(' ')).join(', ')}`)).toEqual([]);
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(1);
});
