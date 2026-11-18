import { test, expect, type Page } from '@playwright/test';

// Spec 016: the calibration run in the demo, which runs the device's own run
// (lib/CalibrationLogic) on the simulated roof. A short roof (200 mm) so a run
// takes seconds: start, watch the legs with Stop shown, accept the proposal;
// a Stop mid-leg ends it with nothing proposed. Its accessibility scan is in a11y.spec.ts.

const deviceConfig = (page: Page) => page.evaluate(async () => (await fetch('/api/config')).json());
const saveRoof = (page: Page, roof: Record<string, unknown>) =>
  page.evaluate(
    (roof) => fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ roof }) }),
    roof,
  );

const openPage = async (page: Page) => {
  await page.goto('./?panel=hidden#/');
  await expect(page.locator('main')).toBeVisible();
  await saveRoof(page, { travelMm: 200, speedMmPerSecond: 38, topDutyPercent: 100, slowDutyPercent: 40 });
  await page.goto('./?panel=hidden#/calibrate');
  await expect(page.getByRole('heading', { level: 2, name: 'Calibration run' })).toBeVisible();
};

const startRun = async (page: Page) => {
  await page.getByRole('switch', { name: 'I am at the roof and can see it' }).check();
  await page.locator('[data-field="calibrate.travelMm"]').fill('200');
  await page.locator('[data-field="calibrate.travelMm"]').blur();
  const start = page.getByRole('button', { name: 'Start calibration' });
  // The device's first seconds: the weather and the park are still being read.
  await expect(start).toBeEnabled({ timeout: 20_000 });
  await start.click();
};

test('a run measures the roof and its proposal is saved on Accept', async ({ page }) => {
  test.setTimeout(120_000);
  await openPage(page);
  await startRun(page);
  const stop = page.getByRole('button', { name: 'Stop calibration' });
  await expect(stop).toBeVisible({ timeout: 10_000 });
  await expect(stop).toBeEnabled();
  await expect(page.locator('.stop-bar')).toBeVisible();
  // After the slow open the person confirms the roof is open (the demo's roof is).
  await page.getByRole('button', { name: 'Yes, it is open' }).click({ timeout: 60_000 });
  await expect(page.getByRole('button', { name: 'Accept and save' })).toBeVisible({ timeout: 90_000 });
  await expect(page.locator('#calibrate-result')).toContainText('Calibration finished');
  await expect(page.locator('#calibrate-result')).toContainText('Slow open');
  await page.getByRole('button', { name: 'Accept and save' }).click();
  await expect(page.locator('#calibrate-result')).toContainText('Saved');
  const config = await deviceConfig(page);
  expect(config.roof.travelMm).toBe(200);
  expect(config.roof.hallHighEnd).toBe('open');
});

test('Stop mid-leg ends the run and proposes nothing', async ({ page }) => {
  test.setTimeout(60_000);
  await openPage(page);
  const before = (await deviceConfig(page)).roof;
  await startRun(page);
  const stop = page.getByRole('button', { name: 'Stop calibration' });
  await expect(stop).toBeVisible({ timeout: 10_000 });
  await page.waitForTimeout(2500);
  await stop.click();
  await expect(page.locator('#calibrate-result')).toContainText('Stopped by a person', { timeout: 10_000 });
  await expect(page.getByRole('button', { name: 'Accept and save' })).toHaveCount(0);
  expect((await deviceConfig(page)).roof).toEqual(before);
});

// Spec 019: jog and teach the ends in the demo: the device's own jog, its
// keep-alive deadline and the teach (lib/CalibrationLogic) on the simulated roof.
const teach = (page: Page) => page.evaluate(async () => (await fetch('/api/roof/teach')).json());
const roofState = (page: Page) => page.evaluate(async () => (await (await fetch('/api/status')).json()).roof.state as string);

const holdFor = async (page: Page, name: string, ms: number) => {
  await page.getByRole('button', { name }).hover();
  await page.mouse.down();
  await page.waitForTimeout(ms);
  await page.mouse.up();
};

const mark = async (page: Page, name: string) => {
  const button = page.locator('#teach').getByRole('button', { name });
  await expect(button).toBeEnabled({ timeout: 10_000 });
  await button.click();
};

test('jog to each end, mark it, and the confirmed proposal is saved', async ({ page }) => {
  test.setTimeout(120_000);
  await openPage(page);
  await page.getByRole('switch', { name: 'I am at the roof, watching it' }).check();
  await expect(page.getByRole('button', { name: 'Hold to open' })).toBeEnabled({ timeout: 20_000 });
  await mark(page, 'This is closed');
  await holdFor(page, 'Hold to open', 3000);
  await expect.poll(() => roofState(page), { timeout: 5_000 }).not.toMatch(/opening|closing/);
  await mark(page, 'This is part-way');
  await holdFor(page, 'Hold to open', 15_000); // stops at the open magnet by itself
  await expect.poll(() => roofState(page), { timeout: 5_000 }).toBe('open');
  await mark(page, 'This is open');
  const confirm = page.getByRole('button', { name: 'Confirm and save' });
  await expect(confirm).toBeVisible({ timeout: 5_000 });
  const proposal = (await teach(page)).proposal;
  expect(proposal.ok).toBe(true);
  await confirm.click();
  await expect(page.locator('#teach')).toContainText('Saved.');
  const roof = (await deviceConfig(page)).roof;
  expect(roof.hallHighEnd).toBe(proposal.hallHighEnd);
  expect(roof.hallHighCounts).toBe(proposal.hallHighCounts);
  expect(roof.hallLowCounts).toBe(proposal.hallLowCounts);
  expect(await roofState(page)).toBe('open');
});

test('a jog with no keep-alive stops by itself', async ({ page }) => {
  test.setTimeout(60_000);
  await openPage(page);
  await page.getByRole('switch', { name: 'I am at the roof, watching it' }).check();
  await expect(page.getByRole('button', { name: 'Hold to open' })).toBeEnabled({ timeout: 20_000 });
  const started = await page.evaluate(async () =>
    (
      await fetch('/api/roof/jog', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-RoofRoof': '1' },
        body: JSON.stringify({ direction: 'open', attended: true }),
      })
    ).json(),
  );
  expect(started.success).toBe(true);
  await page.waitForTimeout(1500);
  expect((await teach(page)).jog.active).toBe(false);
  expect(await roofState(page)).not.toMatch(/opening|closing/);
});
