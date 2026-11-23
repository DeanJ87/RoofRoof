import { test, expect, type Page } from '@playwright/test';

// Specs 015, 018 and 025: the setup wizard in the demo, which runs the device's
// own settings code and its simulated roof. Walk the whole journey on the real
// observatory branch (jog, teach and the calibration run included) and on the
// test box branch, never leaving the wizard; reload mid-way and resume; leave
// and skip; run it from System; with the roof moving, Stop stays above the
// dialog, reachable by keyboard, and a save is refused.

const dialog = (page: Page) => page.getByRole('dialog');
const question = (page: Page) => dialog(page).locator('.speech-bubble h2');
const onScreen = (page: Page, id: string) =>
  expect(page.locator('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id, { timeout: 15_000 });
const proceed = (page: Page) => dialog(page).getByRole('button', { name: 'Continue' }).click();
// A choice card is a label around a hidden input: click the card, as a person does.
const choose = async (page: Page, name: string | RegExp) => {
  const input = page.getByRole('radio', { name, exact: typeof name === 'string' });
  await dialog(page).locator('label.choice').filter({ has: input }).click();
  await expect(dialog(page).getByRole('radio', { name, exact: typeof name === 'string' })).toBeChecked();
};
const tick = async (page: Page, name: RegExp) => {
  await dialog(page)
    .locator('label.choice')
    .filter({ has: page.getByRole('checkbox', { name }) })
    .click();
};
const deviceConfig = (page: Page) => page.evaluate(async () => (await fetch('/api/config')).json());
const saveConfig = (page: Page, body: Record<string, unknown>) =>
  page.evaluate(
    (body) => fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }),
    body,
  );
const roofState = (page: Page) => page.evaluate(async () => (await (await fetch('/api/status')).json()).roof.state as string);
const roofCommand = (page: Page, name: string) =>
  page.evaluate((name) => fetch(`/api/roof/${name}`, { method: 'POST', headers: { 'X-RoofRoof': '1' } }).then((r) => r.status), name);

// Every address the page had: the wizard must never leave #/wizard (SC-001).
const watchAddress = (page: Page) =>
  page.evaluate(() => {
    const seen: string[] = [];
    (window as unknown as { seen: string[] }).seen = seen;
    window.addEventListener('hashchange', () => seen.push(location.hash));
  });
const addresses = (page: Page) => page.evaluate(() => (window as unknown as { seen: string[] }).seen);

const restartWizard = async (page: Page, before?: Record<string, unknown>) => {
  await page.goto('./');
  await expect(page.locator('main')).toBeVisible();
  if (before) await saveConfig(page, before);
  await page.getByRole('button', { name: /Demo/ }).click();
  await page.getByRole('button', { name: 'Restart setup wizard' }).click();
  await expect(question(page)).toHaveText('What should we call this roof?', { timeout: 15_000 });
};

// A short simulated roof, so the moves take seconds.
const SHORT_ROOF = { roof: { travelMm: 200, speedMmPerSecond: 38, topDutyPercent: 100, slowDutyPercent: 40 } };

// A mouse hold, as a person does; the roof must really move while it is held (a note or Stop
// appearing must not move the button from under the pointer and end the hold).
const holdFor = async (page: Page, name: string, ms: number) => {
  await dialog(page).getByRole('button', { name }).hover();
  await page.mouse.down();
  await expect.poll(() => roofState(page), { timeout: 3_000, intervals: [100] }).toMatch(/opening|closing/);
  await page.waitForTimeout(ms);
  await page.mouse.up();
  await expect.poll(() => roofState(page), { timeout: 5_000 }).not.toMatch(/opening|closing/);
};
// Held until the roof stops at `end` by itself (its magnet), as a person would hold it.
const holdTo = async (page: Page, name: string, end: 'open' | 'closed') => {
  if ((await roofState(page)) === end) return;
  await dialog(page).getByRole('button', { name }).hover();
  await page.mouse.down();
  await expect.poll(() => roofState(page), { timeout: 30_000, intervals: [200] }).toBe(end);
  await page.mouse.up();
};
const mark = async (page: Page, name: string) => {
  const button = dialog(page).getByRole('button', { name });
  await expect(button).toBeEnabled({ timeout: 10_000 });
  await button.click();
};
const atTheRoof = (page: Page, name: string) => dialog(page).getByRole('switch', { name }).check();

// The motor part: the direction check, the teach and the calibration run, all in the dialog.
const motorPart = async (page: Page) => {
  await onScreen(page, 'direction');
  await atTheRoof(page, 'I am at the roof, watching it');
  await expect(dialog(page).getByRole('button', { name: 'Hold to open' })).toBeEnabled({ timeout: 20_000 });
  await holdFor(page, 'Hold to open', 1500);
  await choose(page, /Yes, it opened/);
  await proceed(page);

  await onScreen(page, 'teach');
  const before = (await deviceConfig(page)).roof;
  await atTheRoof(page, 'I am at the roof, watching it');
  await holdTo(page, 'Hold to close', 'closed'); // stops at the closed magnet by itself
  await mark(page, 'This is closed');
  await holdFor(page, 'Hold to open', 2500);
  await mark(page, 'This is part-way');
  await holdTo(page, 'Hold to open', 'open');
  await mark(page, 'This is open');
  await dialog(page).getByRole('button', { name: 'Confirm and save' }).click({ timeout: 10_000 });
  await expect(dialog(page).locator('#teach')).toContainText('Saved.');
  const taught = (await deviceConfig(page)).roof;
  expect([taught.hallLowCounts, taught.hallHighCounts]).not.toEqual([before.hallLowCounts, before.hallHighCounts]);
  await proceed(page);

  await onScreen(page, 'calibrate');
  await atTheRoof(page, 'I am at the roof and can see it');
  await page.locator('[data-field="calibrate.travelMm"]').fill('210'); // the tape's travel, not the 200 mm the roof had
  await page.locator('[data-field="calibrate.travelMm"]').blur();
  const start = dialog(page).getByRole('button', { name: 'Start calibration' });
  await expect(start).toBeEnabled({ timeout: 20_000 });
  await start.click();
  await expect(dialog(page).getByRole('button', { name: 'Stop calibration' })).toBeVisible({ timeout: 10_000 });
  await dialog(page).getByRole('button', { name: 'Yes, it is open' }).click({ timeout: 60_000 });
  await dialog(page).getByRole('button', { name: 'Accept and save' }).click({ timeout: 90_000 });
  await expect(dialog(page).locator('#calibrate-result')).toContainText('Saved');
  await proceed(page);
};

test('the real observatory branch: every screen answered in the wizard, the motor included', async ({ page }) => {
  test.setTimeout(300_000);
  await restartWizard(page, SHORT_ROOF);
  await watchAddress(page);
  await expect(dialog(page)).toHaveAttribute('aria-modal', 'true');
  await expect(page.locator('header.app-header')).toHaveAttribute('inert', '');

  await page.locator('[data-field="deviceName"]').fill('Backyard roof');
  await proceed(page);

  // Spec 023: the board (the demo's DevKit has no network question), then the wiring check.
  await onScreen(page, 'board');
  await choose(page, 'ESP32 DevKit');
  await proceed(page);
  await onScreen(page, 'wiring');
  await dialog(page).getByRole('button', { name: 'Check wiring' }).click();
  await expect(dialog(page).locator('[data-wiring-check="enPulldown"]')).toContainText('Pass', { timeout: 10_000 });
  await expect(dialog(page).getByRole('button', { name: 'Open Jog and teach' })).toHaveCount(0);
  await proceed(page);

  await onScreen(page, 'testBox');
  await expect(dialog(page).getByRole('button', { name: 'Continue' })).toBeDisabled();
  await choose(page, 'Yes, or there will be');
  await proceed(page);

  // SQMeter once: the address, its copy and the weather; its ntfy topic is copied (MQTT left as it is).
  await onScreen(page, 'sqmeter');
  await expect(dialog(page).locator('[data-field="safety.http.url"]')).toHaveCount(1);
  await dialog(page).getByRole('button', { name: 'Copy from SQMeter' }).click();
  await expect(dialog(page).getByRole('switch', { name: 'ntfy topic' })).toBeChecked({ timeout: 15_000 });
  await dialog(page).getByRole('switch', { name: 'MQTT broker' }).uncheck();
  await dialog(page).getByRole('button', { name: 'Apply ticked' }).click();
  await expect(dialog(page).getByRole('switch', { name: 'Weather from SQMeter' })).toBeChecked();
  // The demo's location and time follow SQMeter: shown as done, with Edit, and not asked again.
  await expect(dialog(page).locator('#sqmeter-done-location')).toBeVisible();
  await expect(dialog(page).locator('#sqmeter-done-time')).toBeVisible();
  await proceed(page);
  await onScreen(page, 'daylight');
  await choose(page, 'Alert and close');
  await proceed(page);

  await onScreen(page, 'parkSwitches');
  await choose(page, 'One switch');
  await expect(dialog(page)).toContainText('Park switch 1');
  await proceed(page);
  await onScreen(page, 'nina');
  await dialog(page).getByRole('button', { name: 'Test', exact: true }).click();
  await expect(page.locator('[data-test-result="nina"]')).toContainText('At park', { timeout: 20_000 });
  await expect(dialog(page).getByRole('switch', { name: /Open and close over Alpaca/ })).toBeChecked();
  await proceed(page);

  await motorPart(page);

  // Several channels as in Settings, each with its fields here.
  await onScreen(page, 'alerts');
  if (!(await dialog(page).getByRole('checkbox', { name: /^ntfy/ }).isChecked())) await tick(page, /^ntfy/);
  await expect(dialog(page).getByRole('checkbox', { name: /^ntfy/ })).toBeChecked();
  await expect(dialog(page).locator('[data-field="alerts.ntfy.topic"]')).toHaveValue('darkskylab-sqm');
  await proceed(page);

  await onScreen(page, 'password');
  await choose(page, /Set a password/);
  await expect(dialog(page).getByRole('button', { name: 'Continue' })).toBeDisabled();
  await page.locator('[data-field="auth.password"]').fill('night-sky');
  await proceed(page);

  await onScreen(page, 'review');
  await expect(page.locator('[data-check]')).toHaveCount(14);
  for (const id of ['testBox', 'password', 'weather', 'nina', 'motion', 'hall'])
    await expect(page.locator(`[data-check="${id}"]`), id).toHaveAttribute('data-state', 'ok');
  await page.getByRole('button', { name: 'Finish' }).click();
  await expect(question(page)).toHaveText('Your roof is set up.');

  const config = await deviceConfig(page);
  expect(config.deviceName).toBe('Backyard roof');
  expect(config.auth.enabled).toBe(true);
  expect(config.location.source).toBe('sqmeter');
  expect(config.alerts.ntfy).toMatchObject({ enabled: true, topic: 'darkskylab-sqm' });
  expect(config.park.switches).toBe('one');
  expect(config.daylight.mode).toBe('close');
  expect(config.roof.travelMm).toBe(210); // the run's proposal, accepted in the wizard
  expect(config.setup).toEqual({ state: 'done', step: 0 });
  // SC-001: the address never left the wizard.
  expect((await addresses(page)).filter((hash) => !hash.startsWith('#/wizard'))).toEqual([]);

  await page.getByRole('button', { name: 'Go to dashboard' }).click();
  await expect(dialog(page)).toHaveCount(0);
  await expect(page.locator('[data-inventory="setup-banner"]')).toHaveCount(0);
});

test('the test box branch: SQMeter for site data only, no N.I.N.A., the bypasses named on the motor screens', async ({ page }) => {
  test.setTimeout(120_000);
  const bench = {
    ...SHORT_ROOF,
    park: { nina: { url: '' } },
    safety: { http: { enabled: false }, mqtt: { enabled: false } },
  };
  await restartWizard(page, bench);
  await proceed(page); // the name
  await onScreen(page, 'board');
  await proceed(page);
  await onScreen(page, 'wiring');
  await proceed(page);
  await onScreen(page, 'testBox');
  await choose(page, /No telescope yet/);
  await expect(dialog(page)).toContainText('Never with a telescope');
  await proceed(page);

  await onScreen(page, 'sqmeter');
  await expect(dialog(page)).toContainText('Test box: the weather check is bypassed, so SQMeter supplies only site data.');
  await expect(dialog(page).getByRole('switch', { name: 'Weather from SQMeter' })).toHaveCount(0);
  await proceed(page);
  await onScreen(page, 'daylight');
  await choose(page, 'Do nothing');
  await proceed(page);
  await onScreen(page, 'parkSwitches');
  await choose(page, 'None');
  await proceed(page);

  // No N.I.N.A. screen: the park check is bypassed, and the motor screens say so.
  await onScreen(page, 'direction');
  await expect(dialog(page).locator('[data-motor]')).toContainText(
    'Test box: park check (no telescope), weather check (no weather source)',
  );
  await atTheRoof(page, 'I am at the roof, watching it');
  await expect(dialog(page).getByRole('button', { name: 'Hold to open' })).toBeEnabled({ timeout: 20_000 });
  await holdFor(page, 'Hold to open', 1500);
  await choose(page, /No, it closed/);
  await expect(dialog(page)).toContainText("swap the motor's two wires");
  await expect(dialog(page).getByRole('button', { name: 'Continue' })).toBeDisabled();
  // No motor yet? The typed values, then on to the alerts.
  await dialog(page).getByRole('button', { name: 'Enter manually instead' }).click();
  await onScreen(page, 'motion');
  await expect(dialog(page).locator('[data-field="roof.travelMm"]')).toBeVisible();
  await proceed(page);
  await onScreen(page, 'alerts');
  await tick(page, /No alerts for now/);
  await expect(dialog(page)).toContainText('No alert channel');
  await proceed(page);
  await onScreen(page, 'password');
  const config = await deviceConfig(page);
  expect(config.testBox).toMatchObject({ park: true, weather: true, daylight: true });
  expect(config.alerts.pushover.enabled).toBe(false);
});

test('a reload mid-way resumes at the same part with the answers kept; "No password" stays', async ({ page }) => {
  test.setTimeout(120_000);
  await restartWizard(page);
  await proceed(page); // the name
  await onScreen(page, 'board');
  await proceed(page);
  await onScreen(page, 'wiring');
  await dialog(page).getByRole('button', { name: 'Skip for now' }).click(); // Skip records the next part too
  await onScreen(page, 'testBox');
  await expect.poll(async () => (await deviceConfig(page)).setup.step).toBe(3);
  await page.reload();
  await onScreen(page, 'testBox');
  await choose(page, 'Yes, or there will be');
  await proceed(page);
  await onScreen(page, 'sqmeter');
  await page.reload();
  await onScreen(page, 'sqmeter');

  // Straight to the password part (a link) and back to the dashboard: the wizard opens by itself once per load.
  await page.goto('./#/wizard?screen=password');
  await page.reload();
  await onScreen(page, 'password');
  await expect(page).toHaveURL(/#\/wizard$/); // the explicit start applies once
  await choose(page, 'No password');
  await proceed(page);
  await onScreen(page, 'review');
  await page.goto('./#/');
  await page.reload();
  await onScreen(page, 'review');
  await dialog(page).getByRole('button', { name: 'Back' }).click();
  await onScreen(page, 'password');
  await expect(dialog(page).getByRole('radio', { name: 'No password' })).toBeChecked();
  await expect(dialog(page).getByRole('button', { name: 'Continue' })).toBeEnabled();
  expect((await deviceConfig(page)).auth.enabled).toBe(false);

  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: 'Leave for now' }).click();
  const banner = page.locator('[data-inventory="setup-banner"]');
  await expect(banner).toContainText("Setup isn't finished");
  await banner.getByRole('button', { name: 'Continue setup' }).click();
  await onScreen(page, 'review'); // the part saved last
});

test('a re-run from System resumes where it was after a reload', async ({ page }) => {
  await restartWizard(page);
  await dialog(page).getByRole('button', { name: 'Close setup' }).click();
  await dialog(page).getByRole('button', { name: 'Skip setup' }).click();
  await expect(page.locator('[data-leave]')).toContainText('may not close itself in bad weather');
  await page.getByRole('button', { name: 'Skip setup anyway' }).click();
  await expect(dialog(page)).toHaveCount(0, { timeout: 15_000 });
  expect((await deviceConfig(page)).setup.state).toBe('done');
  await page.goto('./#/system');
  await page.getByRole('button', { name: 'Run setup wizard' }).click();
  await onScreen(page, 'name');
  await proceed(page);
  await onScreen(page, 'board');
  await proceed(page);
  await onScreen(page, 'wiring');
  await page.reload();
  await onScreen(page, 'wiring');
  // Leaving goes back to the page it was opened from.
  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: 'Leave for now' }).click();
  await expect(dialog(page)).toHaveCount(0);
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
  // Focus stays in the dialog (on the reason), so Escape still works.
  await expect(page.locator('#wizard-problem-box')).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(question(page)).toHaveText('Leave setup?');
  await page.keyboard.press('Escape');
  await onScreen(page, 'name');

  await stop.click();
  await expect.poll(() => roofState(page), { timeout: 10_000 }).not.toMatch(/opening|closing/);
});

test('keyboard: arrows move through the choices, Escape asks before leaving, focus goes back to the page', async ({ page }) => {
  await restartWizard(page);
  for (let n = 0; n < 20 && (await page.locator('[data-wizard-screen]').getAttribute('data-wizard-screen')) !== 'password'; n++) {
    const id = await page.locator('[data-wizard-screen]').getAttribute('data-wizard-screen');
    await dialog(page).getByRole('button', { name: 'Skip for now' }).click();
    await expect(page.locator('[data-wizard-screen]')).not.toHaveAttribute('data-wizard-screen', id!);
  }
  await onScreen(page, 'password');
  const on = dialog(page).getByRole('radio', { name: /Set a password/ });
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
