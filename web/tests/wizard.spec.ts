import { test, expect, type Page } from '@playwright/test';

// Spec 015: the setup wizard in the demo, which runs the device's own
// settings code. Restart it from the Demo panel, walk every page saving each,
// Test SQMeter and N.I.N.A., finish; resume after a reload; skip; run it from
// System; a save while the roof moves is refused and the Stop bar is there;
// no page offers a roof command.

const restartWizard = async (page: Page) => {
  await page.goto('./');
  await expect(page.locator('main')).toBeVisible();
  await page.getByRole('button', { name: /Demo/ }).click();
  await page.getByRole('button', { name: 'Restart setup wizard' }).click();
  await expect(heading(page)).toHaveText('Step 1 of 11: Device', { timeout: 15_000 });
};

const heading = (page: Page) => page.locator('.wizard-heading');
const saveAndContinue = (page: Page) => page.getByRole('button', { name: 'Save and continue' }).click();
const deviceConfig = (page: Page) => page.evaluate(async () => (await fetch('/api/config')).json());
const roofCommand = (page: Page, name: string) =>
  page.evaluate((name) => fetch(`/api/roof/${name}`, { method: 'POST', headers: { 'X-RoofRoof': '1' } }).then((r) => r.status), name);

test('a new device walks through every page and finishes on the review', async ({ page }) => {
  test.setTimeout(120_000);
  await restartWizard(page);

  // 1 Device: a password is offered by default.
  await expect(page.getByRole('switch', { name: 'Password-protect changes' })).toBeChecked();
  await page.locator('[data-field="deviceName"]').fill('Backyard roof');
  await page.locator('[data-field="auth.password"]').fill('night-sky');
  await saveAndContinue(page);

  // 2 Site and time.
  await expect(heading(page)).toHaveText('Step 2 of 11: Site and time');
  await page.getByRole('switch', { name: 'Location set' }).check();
  await page.locator('[data-field="location.latitude"]').fill('51.5');
  await page.locator('[data-field="location.longitude"]').fill('-0.1');
  await saveAndContinue(page);

  // 3 Hardware: the live supply, compared with a meter reading.
  await expect(heading(page)).toHaveText('Step 3 of 11: Hardware');
  await expect(page.locator('#supply')).toContainText(/\d+\.\d+ V/);
  await page.locator('[data-field="wizard.meter"]').fill('12.6');
  await page.locator('[data-field="wizard.meter"]').blur();
  await expect(page.locator('#supply')).toContainText('agrees with your meter');
  await page.locator('[data-field="park.switches"]').selectOption('one');
  await saveAndContinue(page);

  // 4 Hall sensor: the live reading against the thresholds.
  await expect(heading(page)).toHaveText('Step 4 of 11: Hall sensor');
  await expect(page.getByRole('img', { name: /^Hall reading \d+ of 4095; low threshold/ })).toBeVisible();
  await saveAndContinue(page);

  // 5 Calibration run (spec 016): offered, linked, nothing moves here.
  await expect(heading(page)).toHaveText('Step 5 of 11: Calibration run');
  await expect(page.getByRole('button', { name: 'Open the calibration page' })).toBeVisible();
  await saveAndContinue(page);

  // 6 Roof motion.
  await expect(heading(page)).toHaveText('Step 6 of 11: Roof motion');
  await saveAndContinue(page);

  // 7 Weather: Test shows the simulated SQMeter's answer.
  await expect(heading(page)).toHaveText('Step 7 of 11: Weather');
  await page.getByRole('button', { name: 'Test', exact: true }).click();
  await expect(page.locator('[data-test-result="weather"]')).toContainText('Safe', { timeout: 20_000 });
  await saveAndContinue(page);

  // 8 N.I.N.A.: Test shows the mount.
  await expect(heading(page)).toHaveText('Step 8 of 11: N.I.N.A.');
  await expect(page.locator('#alpaca')).toContainText('this device is the failsafe');
  await page.getByRole('button', { name: 'Test', exact: true }).click();
  const nina = page.locator('[data-test-result="nina"]');
  await expect(nina).toContainText('Mount connected', { timeout: 20_000 });
  await expect(nina).toContainText('At park');
  await saveAndContinue(page);

  // 9 Alerts: the demo's ntfy channel is on; a test goes out.
  await expect(heading(page)).toHaveText('Step 9 of 11: Alerts');
  await page.getByRole('button', { name: 'Send test' }).first().click();
  await saveAndContinue(page);

  // 10 Daylight.
  await expect(heading(page)).toHaveText('Step 10 of 11: Daylight');
  await page.locator('[data-field="daylight.mode"]').selectOption('close');
  await saveAndContinue(page);

  // 11 Review, then Finish.
  await expect(heading(page)).toHaveText('Step 11 of 11: Review');
  await expect(page.locator('[data-check]')).toHaveCount(13);
  await expect(page.locator('[data-check="password"]')).toHaveAttribute('data-state', 'ok');
  await expect(page.locator('[data-check="weather"]')).toHaveAttribute('data-state', 'ok');
  await expect(page.locator('[data-check="nina"]')).toHaveAttribute('data-state', 'ok');
  await expect(page.getByRole('link', { name: 'Gate for unattended use' })).toBeVisible();
  await page.getByRole('button', { name: 'Finish' }).click();
  await expect(page.getByText(/Setup finished/)).toBeVisible();

  const config = await deviceConfig(page);
  expect(config.deviceName).toBe('Backyard roof');
  expect(config.auth.enabled).toBe(true);
  expect(config.location).toMatchObject({ set: true, latitude: 51.5 });
  expect(config.park.switches).toBe('one');
  expect(config.daylight.mode).toBe('close');
  expect(config.setup).toEqual({ state: 'done', step: 0 });

  await page.getByRole('button', { name: 'Go to dashboard' }).click();
  await expect(page.locator('[data-inventory="roof-card"]')).toBeVisible();
  await expect(page.locator('[data-inventory="setup-banner"]')).toHaveCount(0);
});

test('no page offers a roof command', async ({ page }) => {
  await restartWizard(page);
  for (let n = 1; n <= 11; n++) {
    await page
      .locator('nav[aria-label="Setup pages"] button')
      .nth(n - 1)
      .click();
    await expect(heading(page)).toContainText(`Step ${n} of 11`);
    const main = page.locator('main');
    for (const name of [/^Open$/, /^Close$/, /Clear fault/, /^Stop$/]) await expect(main.getByRole('button', { name })).toHaveCount(0);
  }
});

test('a save while the roof moves is refused, with the Stop bar on the page', async ({ page }) => {
  await restartWizard(page);
  // The device's first seconds: the weather and the park are still being read.
  await expect.poll(() => roofCommand(page, 'open'), { timeout: 15_000, intervals: [1000] }).toBe(200);
  await expect(page.locator('.stop-bar').getByRole('button', { name: 'Stop' })).toBeVisible({ timeout: 10_000 });
  await expect(page.getByText('The roof is moving: saves are refused until it stops')).toBeVisible();
  await page.locator('[data-field="deviceName"]').fill('Moving roof');
  // The Stop bar sits over the bottom of the page while the roof moves: click without a pointer.
  await page.getByRole('button', { name: 'Save and continue' }).evaluate((el: HTMLElement) => el.click());
  await expect(page.locator('#wizard-problem')).toBeVisible();
  await expect(heading(page)).toHaveText('Step 1 of 11: Device');
  await page.locator('.stop-bar').getByRole('button', { name: 'Stop' }).click();
});

test('the wizard resumes where it was left, and opens by itself once per page load', async ({ page }) => {
  await restartWizard(page);
  await page.getByRole('switch', { name: 'Password-protect changes' }).uncheck();
  await saveAndContinue(page);
  await expect(heading(page)).toHaveText('Step 2 of 11: Site and time');
  // A new page load on the dashboard opens the wizard at the saved page.
  await page.goto('./#/');
  await page.reload();
  await expect(heading(page)).toHaveText('Step 2 of 11: Site and time', { timeout: 15_000 });
  // Back on the dashboard in the same load: the banner instead.
  await page
    .getByRole('button', { name: 'Home' })
    .or(page.getByRole('button', { name: 'Dashboard' }))
    .first()
    .click();
  const banner = page.locator('[data-inventory="setup-banner"]');
  await expect(banner).toContainText("Setup isn't finished");
  await banner.getByRole('button', { name: 'Continue setup' }).click();
  await expect(heading(page)).toHaveText('Step 2 of 11: Site and time');
});

test('skipping setup warns first; System runs it again', async ({ page }) => {
  await restartWizard(page);
  await page.getByRole('button', { name: 'Skip setup' }).click();
  await expect(page.locator('[data-skip-warning]')).toContainText('may not close itself in bad weather');
  await page.getByRole('button', { name: 'Skip setup anyway' }).click();
  await expect(page.locator('[data-inventory="roof-card"]')).toBeVisible({ timeout: 15_000 });
  await expect(page.locator('[data-inventory="setup-banner"]')).toHaveCount(0);
  expect((await deviceConfig(page)).setup.state).toBe('done');
  await page.goto('./#/system');
  await page.getByRole('button', { name: 'Run setup wizard' }).click();
  await expect(heading(page)).toHaveText('Step 1 of 11: Device');
});
