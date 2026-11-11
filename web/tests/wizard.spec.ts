import { test, expect, type Page } from '@playwright/test';

// Specs 015 and 018: the setup wizard in the demo, which runs the device's own
// settings code. Restart it from the Demo panel and walk every screen; Test
// SQMeter and N.I.N.A.; finish; resume after a reload; leave and skip; run it
// from System; with the roof moving, Stop stays above the dialog, reachable by
// keyboard, and a save is refused; no screen offers a roof command.

const dialog = (page: Page) => page.getByRole('dialog');
const question = (page: Page) => dialog(page).locator('.speech-bubble h2');
const onScreen = (page: Page, id: string) => expect(page.locator('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id);
const proceed = (page: Page) => dialog(page).getByRole('button', { name: 'Continue' }).click();
// A choice card is a label around a hidden radio: click the card, as a person does.
const choose = async (page: Page, name: string) => {
  const radio = dialog(page).getByRole('radio', { name, exact: true });
  await dialog(page)
    .locator('label.choice')
    .filter({ has: page.getByRole('radio', { name, exact: true }) })
    .click();
  await expect(radio).toBeChecked();
};
const deviceConfig = (page: Page) => page.evaluate(async () => (await fetch('/api/config')).json());
const roofCommand = (page: Page, name: string) =>
  page.evaluate((name) => fetch(`/api/roof/${name}`, { method: 'POST', headers: { 'X-RoofRoof': '1' } }).then((r) => r.status), name);

const restartWizard = async (page: Page) => {
  await page.goto('./');
  await expect(page.locator('main')).toBeVisible();
  await page.getByRole('button', { name: /Demo/ }).click();
  await page.getByRole('button', { name: 'Restart setup wizard' }).click();
  await expect(question(page)).toHaveText('What should we call this roof?', { timeout: 15_000 });
};

test('a new device answers every screen and finishes', async ({ page }) => {
  test.setTimeout(150_000);
  await restartWizard(page);
  await expect(dialog(page)).toHaveAttribute('aria-modal', 'true');
  await expect(page.locator('header.app-header')).toHaveAttribute('inert', '');

  await page.locator('[data-field="deviceName"]').fill('Backyard roof');
  await proceed(page);

  // A password is chosen for the owner when none is set; Continue waits for one.
  await onScreen(page, 'password');
  await expect(dialog(page).getByRole('radio', { name: 'Set a password' })).toBeChecked();
  await expect(dialog(page).getByRole('button', { name: 'Continue' })).toBeDisabled();
  await page.locator('[data-field="auth.password"]').fill('night-sky');
  await proceed(page);

  await onScreen(page, 'site');
  await page.getByRole('switch', { name: 'Location set' }).check();
  await page.locator('[data-field="location.latitude"]').fill('51.5');
  await page.locator('[data-field="location.longitude"]').fill('-0.1');
  await proceed(page);
  await onScreen(page, 'time');
  await proceed(page);

  // The live supply, compared with a meter reading.
  await onScreen(page, 'supply');
  await expect(page.locator('#supply')).toContainText(/\d+\.\d+ V/);
  await page.locator('[data-field="wizard.meter"]').fill('12.6');
  await page.locator('[data-field="wizard.meter"]').blur();
  await expect(page.locator('#supply')).toContainText('agrees with your meter');
  await proceed(page);

  // Choice screens start unanswered on a new device.
  await onScreen(page, 'current');
  await expect(dialog(page).getByRole('button', { name: 'Continue' })).toBeDisabled();
  await choose(page, 'Not fitted yet');
  await proceed(page);
  await onScreen(page, 'parkSwitches');
  await choose(page, 'One switch');
  await expect(dialog(page)).toContainText('Park switch 1');
  await proceed(page);
  await onScreen(page, 'rain');
  await choose(page, 'Not fitted');
  await proceed(page);

  await onScreen(page, 'hallFitted');
  await choose(page, 'Fitted');
  await proceed(page);
  await onScreen(page, 'hall');
  await expect(dialog(page).getByRole('img', { name: /^Hall reading \d+ of 4095; low threshold/ })).toBeVisible();
  await proceed(page);

  await onScreen(page, 'calibrate');
  await expect(page.getByRole('button', { name: 'Open the calibration page' })).toBeVisible();
  await proceed(page);
  await onScreen(page, 'motion');
  await proceed(page);

  await onScreen(page, 'weatherSource');
  await choose(page, "SQMeter's web address");
  await proceed(page);
  await onScreen(page, 'weatherAddress');
  await page.getByRole('button', { name: 'Test', exact: true }).click();
  await expect(page.locator('[data-test-result="weather"]')).toContainText('Safe', { timeout: 20_000 });
  await proceed(page);

  await onScreen(page, 'nina');
  await page.getByRole('button', { name: 'Test', exact: true }).click();
  const nina = page.locator('[data-test-result="nina"]');
  await expect(nina).toContainText('Mount connected', { timeout: 20_000 });
  await expect(nina).toContainText('At park');
  await proceed(page);
  await onScreen(page, 'alpaca');
  await choose(page, 'Yes, over Alpaca');
  await expect(page.locator('#alpaca')).toContainText('this device is the failsafe');
  await proceed(page);

  // The demo's ntfy channel: a test goes out once saved.
  await onScreen(page, 'alertChannel');
  await choose(page, 'ntfy');
  await proceed(page);
  await onScreen(page, 'alertDetails');
  await page.getByRole('button', { name: 'Send test' }).first().click();
  await proceed(page);

  await onScreen(page, 'daylight');
  await choose(page, 'Alert and close');
  await proceed(page);

  await onScreen(page, 'review');
  await expect(page.locator('[data-check]')).toHaveCount(13);
  await expect(page.locator('[data-check="password"]')).toHaveAttribute('data-state', 'ok');
  await expect(page.locator('[data-check="weather"]')).toHaveAttribute('data-state', 'ok');
  await expect(page.locator('[data-check="nina"]')).toHaveAttribute('data-state', 'ok');
  await page.getByRole('button', { name: 'Finish' }).click();
  await expect(question(page)).toHaveText('Your roof is set up.');
  await expect(page.getByRole('link', { name: 'Gate for unattended use' })).toBeVisible();

  const config = await deviceConfig(page);
  expect(config.deviceName).toBe('Backyard roof');
  expect(config.auth.enabled).toBe(true);
  expect(config.location).toMatchObject({ set: true, latitude: 51.5 });
  expect(config.park.switches).toBe('one');
  expect(config.daylight.mode).toBe('close');
  expect(config.setup).toEqual({ state: 'done', step: 0 });

  await page.getByRole('button', { name: 'Go to dashboard' }).click();
  await expect(dialog(page)).toHaveCount(0);
  await expect(page.locator('[data-inventory="roof-card"]')).toBeVisible();
  await expect(page.locator('[data-inventory="setup-banner"]')).toHaveCount(0);
});

test('no screen offers a roof command', async ({ page }) => {
  await restartWizard(page);
  for (let n = 0; n < 25; n++) {
    for (const name of [/^Open$/, /^Close$/, /Clear fault/, /^Stop/])
      await expect(dialog(page).locator('.modal-panel').getByRole('button', { name })).toHaveCount(0);
    const id = await page.locator('[data-wizard-screen]').getAttribute('data-wizard-screen');
    if (id === 'review') break;
    await dialog(page).getByRole('button', { name: 'Skip for now' }).click();
    await expect(page.locator('[data-wizard-screen]')).not.toHaveAttribute('data-wizard-screen', id!);
  }
  await onScreen(page, 'review');
});

test('with the roof moving, Stop is above the dialog, in its focus loop, and a save is refused', async ({ page }) => {
  await restartWizard(page);
  // The device's first seconds: the weather and the park are still being read.
  await expect.poll(() => roofCommand(page, 'open'), { timeout: 15_000, intervals: [1000] }).toBe(200);
  const stop = dialog(page).getByRole('button', { name: 'Stop roof' });
  await expect(stop).toBeVisible({ timeout: 10_000 });
  await expect(stop).toBeEnabled();
  // Nothing covers it: it is the topmost element at its centre, and the footer sits above it.
  const box = (await stop.boundingBox())!;
  const topmost = await page.evaluate(
    ([x, y]) => document.elementFromPoint(x, y)?.closest('button')?.textContent,
    [box.x + box.width / 2, box.y + box.height / 2],
  );
  expect(topmost).toBe('Stop roof');
  const footer = (await dialog(page).locator('.modal-footer').boundingBox())!;
  expect(footer.y + footer.height).toBeLessThanOrEqual(box.y);
  await expect(page.getByText('The roof is moving: saves are refused until it stops')).toBeVisible();

  // Tab reaches Stop, and wraps from it back into the dialog.
  let reached = false;
  for (let i = 0; i < 15 && !reached; i++) {
    await page.keyboard.press('Tab');
    reached = await stop.evaluate((el) => el === document.activeElement);
  }
  expect(reached).toBe(true);
  await page.keyboard.press('Tab');
  expect(await page.evaluate(() => document.querySelector('[role="dialog"]')!.contains(document.activeElement))).toBe(true);

  await page.locator('[data-field="deviceName"]').fill('Moving roof');
  await proceed(page);
  await expect(page.locator('#wizard-problem')).toBeVisible();
  await onScreen(page, 'name');

  await stop.click();
  await expect
    .poll(async () => (await page.evaluate(async () => (await fetch('/api/status')).json())).roof.state, { timeout: 10_000 })
    .not.toMatch(/opening|closing/);
});

test('keyboard: arrows move through the choices, Escape asks before leaving, focus goes back to the page', async ({ page }) => {
  await restartWizard(page);
  await page.getByRole('button', { name: 'Skip for now' }).click();
  await onScreen(page, 'password');
  const on = dialog(page).getByRole('radio', { name: 'Set a password' });
  await on.focus();
  await page.keyboard.press('ArrowDown');
  await expect(dialog(page).getByRole('radio', { name: 'No password' })).toBeChecked();
  await page.keyboard.press('ArrowDown'); // wraps
  await expect(on).toBeChecked();
  await expect(question(page)).not.toBeFocused();

  await page.keyboard.press('Escape');
  await expect(question(page)).toHaveText('Leave setup?');
  await expect(question(page)).toBeFocused();
  await page.keyboard.press('Escape');
  await onScreen(page, 'password');
  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: 'Leave for now' }).click();
  await expect(dialog(page)).toHaveCount(0);
  // Opened from the Demo panel, which has closed: focus goes to the page.
  await expect(page.locator('#main')).toBeFocused();
  await expect(page.locator('[data-inventory="setup-banner"]')).toContainText("Setup isn't finished");
});

test('the wizard resumes where it was left, and opens by itself once per page load', async ({ page }) => {
  await restartWizard(page);
  await proceed(page); // the name
  await choose(page, 'No password');
  await proceed(page);
  await onScreen(page, 'site');
  // A new page load on the dashboard opens the wizard at the saved page.
  await page.goto('./#/');
  await page.reload();
  await expect(page.locator('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', 'site', { timeout: 15_000 });
  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: 'Leave for now' }).click();
  const banner = page.locator('[data-inventory="setup-banner"]');
  await expect(banner).toContainText("Setup isn't finished");
  await banner.getByRole('button', { name: 'Continue setup' }).click();
  await onScreen(page, 'site');
});

test('skipping setup warns first; System runs it again', async ({ page }) => {
  await restartWizard(page);
  await dialog(page).getByRole('button', { name: 'Close setup' }).click();
  await dialog(page).getByRole('button', { name: 'Skip setup' }).click();
  await expect(page.locator('[data-leave]')).toContainText('may not close itself in bad weather');
  await page.getByRole('button', { name: 'Skip setup anyway' }).click();
  await expect(dialog(page)).toHaveCount(0, { timeout: 15_000 });
  await expect(page.locator('[data-inventory="roof-card"]')).toBeVisible();
  await expect(page.locator('[data-inventory="setup-banner"]')).toHaveCount(0);
  expect((await deviceConfig(page)).setup.state).toBe('done');
  await page.goto('./#/system');
  await page.getByRole('button', { name: 'Run setup wizard' }).click();
  await onScreen(page, 'name');
  // Leaving goes back to the page it was opened from.
  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: 'Leave for now' }).click();
  await expect(page.getByRole('button', { name: 'Run setup wizard' })).toBeVisible();
});
