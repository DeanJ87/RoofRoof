import { test, expect, type Page } from '@playwright/test';

// Spec 023: the Pins page in the demo, which runs the device's own pin rules
// and wiring check (lib/BoardLogic) on the simulated roof, a DevKit with its
// preset chosen. Its accessibility scan is in a11y.spec.ts.

const deviceConfig = (page: Page) => page.evaluate(async () => (await fetch('/api/config')).json());

const openPage = async (page: Page) => {
  await page.goto('./?panel=hidden#/pins');
  await expect(page.getByRole('heading', { level: 2, name: 'Board and pins' })).toBeVisible();
  await expect(page.getByRole('radiogroup', { name: 'Board preset' })).toBeVisible({ timeout: 15_000 });
};

const typePin = async (page: Page, fn: string, value: string) => {
  await page.locator(`[data-field="pins.${fn}"]`).fill(value);
  await page.locator(`[data-field="pins.${fn}"]`).blur();
};

test('the DevKit is chosen; Custom shows the device errors and warnings live', async ({ page }) => {
  await openPage(page);
  await expect(page.getByRole('radio', { name: 'ESP32 DevKit', exact: true })).toBeChecked();
  await page.locator('label.choice').filter({ hasText: 'Custom' }).click();
  await typePin(page, 'rpwm', '35');
  await expect(page.locator('[data-pin-issue="rpwm"][data-level="error"]')).toContainText('GPIO35 is input only');
  await expect(page.getByRole('button', { name: 'Save pins' })).toBeDisabled();
  await typePin(page, 'rpwm', '25');
  await typePin(page, 'lpwm', '15');
  await expect(page.locator('[data-pin-issue="lpwm"][data-level="warning"]')).toContainText('strapping pin');
  await expect(page.getByRole('button', { name: 'Save pins' })).toBeEnabled();
  // Nothing is saved until Save pins.
  expect((await deviceConfig(page)).pins.preset).toBe('devkit');
});

test('the wiring check reads the simulated roof and moves nothing', async ({ page }) => {
  await openPage(page);
  await page.getByRole('button', { name: 'Check wiring' }).click();
  await expect(page.locator('[data-wiring-check="enPulldown"]')).toContainText('Pass', { timeout: 10_000 });
  await expect(page.locator('[data-wiring-check="supply"]')).toContainText('V');
  await expect(page.getByRole('button', { name: 'Open Jog and teach' })).toBeVisible();
  const state = await page.evaluate(async () => (await (await fetch('/api/status')).json()).roof.state);
  expect(['closed', 'unknown', 'open']).toContain(state);
});

test('Settings → Roof and System link the Pins page', async ({ page }) => {
  await page.goto('./?panel=hidden#/settings?tab=roof');
  await page.locator('#pins').getByRole('button', { name: 'Open the pins page' }).click();
  await expect(page.getByRole('heading', { level: 2, name: 'Board and pins' })).toBeVisible();
  await page.goto('./?panel=hidden#/system');
  await page.getByRole('button', { name: 'Open the pins page' }).click();
  await expect(page.getByRole('heading', { level: 2, name: 'Board and pins' })).toBeVisible();
});
