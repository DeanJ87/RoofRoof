import { test, expect, type Page } from '@playwright/test';

// Spec 026: a new owner's journey in the demo, which runs the device's own logic. The roof setup
// stages; every refusal or hold explained the same way on the dashboard and on Calibrate, with when
// it clears and its fix; the automatic close's hold released by a person at the roof; the test box's
// weather bypass in one save (no close starts); typing over a value that follows SQMeter keeps it.

const roofCommand = (page: Page, name: string) =>
  page.evaluate((name) => fetch(`/api/roof/${name}`, { method: 'POST', headers: { 'X-RoofRoof': '1' } }).then((r) => r.status), name);
const roof = (page: Page) =>
  page.evaluate(
    async () =>
      (await (await fetch('/api/status')).json()).roof as { state: string; closePhase: string; weather: string; parkConfirmed: boolean },
  );
const panelChoice = async (page: Page, name: string) => {
  const toggle = page.locator('.demo-panel-toggle');
  if ((await toggle.getAttribute('aria-expanded')) !== 'true') await toggle.click();
  await page.getByRole('button', { name, exact: true }).click();
  await page.locator('.demo-panel-head').getByRole('button', { name: 'Close' }).click();
};
const ready = async (page: Page) => {
  await page.goto('./#/');
  await expect(page.locator('[data-inventory="status-card"]')).toBeVisible({ timeout: 15_000 });
  await expect
    .poll(
      async () => {
        const r = await roof(page);
        return `${r.state}:${r.weather}:${r.parkConfirmed}`;
      },
      { timeout: 15_000 },
    )
    .toBe('closed:safe:true');
};
const go = (page: Page, hash: string) => page.evaluate((h) => (window.location.hash = h), hash);
const blocker = (page: Page, id: string) => page.locator(`[data-blocker="${id}"]`).first();
const openRoof = async (page: Page) => {
  await panelChoice(page, 'Clock speed 10x');
  expect(await roofCommand(page, 'open')).toBe(200);
  await expect.poll(async () => (await roof(page)).state, { timeout: 60_000 }).toBe('open');
};

test('a new roof: the setup stages on the dashboard, with the move time limits still defaults', async ({ page }) => {
  await ready(page);
  const card = page.locator('[data-inventory="setup-card"]');
  await expect(card).toBeVisible();
  await expect(card.locator('[data-stage]')).toHaveCount(7);
  await expect(card.locator('[data-stage="calibrated"]')).toContainText('Move time limits are still defaults');
  await card.getByRole('button', { name: /Calibration run/ }).click();
  await expect(page.locator('[data-calibrate-page]')).toBeVisible();
  await go(page, '/settings?tab=roof');
  await expect(page.locator('#travel [data-move-limits]')).toContainText('Move time limit');
});

test('held by a person: explained with when it clears, and released at the roof while safe', async ({ page }) => {
  await ready(page);
  await openRoof(page);
  await panelChoice(page, 'Weather: Unsafe');
  await expect.poll(async () => (await roof(page)).state, { timeout: 30_000 }).toBe('closing');
  expect(await roofCommand(page, 'stop')).toBe(200);
  await panelChoice(page, 'Weather: Safe');
  await expect.poll(async () => (await roof(page)).closePhase, { timeout: 10_000 }).toBe('heldByPerson');
  const held = blocker(page, 'heldByPerson');
  await expect(held).toContainText("Automatic close on hold: it won't close by itself");
  await expect(held).toContainText(/Clears at .+ if the weather stays safe, or once the roof is closed\./);
  // The same words on Calibrate, where it holds the jog and the run.
  await go(page, '/calibrate');
  const onCalibrate = page.locator('#calibrate-blockers [data-blocker="heldByPerson"]');
  await expect(onCalibrate).toContainText("Automatic close on hold: it won't close by itself");
  await onCalibrate.getByRole('button', { name: 'Release hold' }).click();
  await page.getByRole('button', { name: "I'm at the roof: release" }).click();
  await expect.poll(async () => (await roof(page)).closePhase, { timeout: 10_000 }).toBe('idle');
  await expect(page.locator('#calibrate-blockers')).toContainText('Nothing stops a move now.');
});

test('no release while the weather is unsafe: the hold says it clears after a safe spell', async ({ page }) => {
  await ready(page);
  await openRoof(page);
  await panelChoice(page, 'Weather: Unsafe');
  await expect.poll(async () => (await roof(page)).state, { timeout: 30_000 }).toBe('closing');
  expect(await roofCommand(page, 'stop')).toBe(200);
  await expect.poll(async () => (await roof(page)).closePhase, { timeout: 10_000 }).toBe('heldByPerson');
  const held = blocker(page, 'heldByPerson');
  await expect(held).toContainText(/Clears after \d+ min of safe weather/);
  await expect(held.getByRole('button', { name: 'Release hold' })).toHaveCount(0);
  const refused = await page.evaluate(() =>
    fetch('/api/roof/release-hold', {
      method: 'POST',
      headers: { 'X-RoofRoof': '1', 'Content-Type': 'application/json' },
      body: '{"attended":true}',
    }).then((r) => r.status),
  );
  expect(refused).toBe(409);
});

for (const [name, choice, id, fix] of [
  ['the weather is unsafe', /^Weather: Unsafe/, 'weather', 'Weather check settings'],
  ['the mount is not parked', /^Mount: .*: Unpark/, 'park', 'Park check settings'],
  ['the supply is low', /^12 V supply: Low/, 'supply', 'Supply settings'],
] as const) {
  test(`Calibrate explains why the roof can't move when ${name}, with its fix`, async ({ page }) => {
    await ready(page);
    await go(page, '/calibrate');
    await page.locator('.demo-panel-toggle').click();
    await page.getByRole('button', { name: choice }).first().click();
    await page.locator('.demo-panel-head').getByRole('button', { name: 'Close' }).click();
    const row = page.locator(`#calibrate-blockers [data-blocker="${id}"]`);
    await expect(row).toBeVisible({ timeout: 20_000 });
    await expect(row).toContainText(/^.+Clears /);
    await expect(row.getByRole('button', { name: fix })).toBeVisible();
  });
}

test('a jammed move faults with its time limit explained and Clear fault beside it', async ({ page }) => {
  await ready(page);
  await panelChoice(page, 'Clock speed 10x');
  await panelChoice(page, 'Jam: On');
  expect(await roofCommand(page, 'open')).toBe(200);
  await go(page, '/calibrate');
  const fault = page.locator('#calibrate-blockers [data-blocker="fault"]');
  await expect(fault).toBeVisible({ timeout: 60_000 });
  await expect(fault).toContainText('Clears when you press Clear fault');
  await panelChoice(page, 'Jam: Off');
  await fault.getByRole('button', { name: 'Clear fault' }).click();
  await expect(page.locator('#calibrate-blockers [data-blocker="fault"]')).toHaveCount(0, { timeout: 10_000 });
});

test('test box: the weather bypass and the source change in one save, so no close starts', async ({ page }) => {
  await ready(page);
  // A box with no telescope: N.I.N.A.'s address off first, then the roof open.
  await page.evaluate(() =>
    fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ park: { nina: { url: '' } }, testBox: { park: true } }),
    }),
  );
  await openRoof(page);
  await go(page, '/test-box');
  await page.locator('input[data-field="testBox.weather"], [data-field="testBox.weather"] input').first().check({ force: true });
  await expect(page.getByText('Saving also turns the SQMeter weather source off, in the same save.')).toBeVisible();
  await page.getByRole('button', { name: /^(Save|Turn on test box)$/ }).click();
  await expect
    .poll(async () => (await page.evaluate(async () => (await (await fetch('/api/config')).json()).testBox.weather)) as boolean)
    .toBe(true);
  // Watch the automatic close for a while: it never starts.
  for (let i = 0; i < 6; i += 1) {
    expect((await roof(page)).closePhase).toBe('idle');
    await page.waitForTimeout(500);
  }
  expect((await roof(page)).state).toBe('open');
});

test('typing over a value that follows SQMeter sets it here, and it stays', async ({ page }) => {
  await ready(page);
  await go(page, '/settings?tab=network');
  const user = page.locator('[data-field="mqtt.username"]');
  await expect(user).toBeVisible({ timeout: 10_000 });
  await user.fill('roofuser');
  await expect(page.getByText('Set here, not from SQMeter').first()).toBeVisible();
  await page.getByRole('button', { name: 'Save', exact: true }).click();
  await expect.poll(async () => page.evaluate(async () => (await (await fetch('/api/config')).json()).sqmeterSync.mqtt.follow)).toBe(false);
  await page.waitForTimeout(3000);
  expect(await page.evaluate(async () => (await (await fetch('/api/config')).json()).mqtt.username)).toBe('roofuser');
});
