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
  // Spec 020: the location is typed in here, not followed from SQMeter.
  await card.locator('[data-field="location.source"]').selectOption('manual');
  // The save bar covers the last card, so the switch is clicked without a pointer.
  await card.getByRole('switch', { name: 'Location set' }).evaluate((el: HTMLElement) => el.click());
  await card.locator('[data-field="location.latitude"]').fill('51.5');
  await card.locator('[data-field="location.longitude"]').fill('-0.1');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() =>
      page.evaluate(async () => {
        const config = await (await fetch('/api/config')).json();
        return `${config.daylight.mode}:${config.location.source}:${config.location.set}:${config.location.latitude}`;
      }),
    )
    .toBe('close:manual:true:51.5');
});

// Spec 020: Copy from SQMeter lists what differs, applies the ticked rows to the form, and Save keeps them;
// a copied group keeps following SQMeter. (The demo's time already follows SQMeter; its MQTT and ntfy are its own.)
test('Settings → Roof copies site settings from SQMeter, and Save keeps them', async ({ page }) => {
  await page.goto('./#/settings?tab=roof&section=sqmeterCopy');
  const card = page.locator('#sqmeterCopy');
  await expect(card).toBeVisible({ timeout: 15_000 });
  await card.getByRole('button', { name: 'Copy from SQMeter' }).click();
  // The MQTT broker's user name differs: applying it clears the password and asks for it.
  await expect(card.getByRole('switch', { name: 'MQTT broker' })).toBeChecked({ timeout: 15_000 });
  await expect(card).toContainText('SQMeter: observatory@192.168.1.10:1883');
  await expect(card).toContainText('Enter the MQTT password');
  await expect(card).toContainText('Copied settings keep following SQMeter');
  await card.getByRole('switch', { name: 'ntfy topic' }).uncheck();
  await card.getByRole('button', { name: 'Apply ticked' }).click();
  await expect(card).toContainText('Applied: save to keep them.');
  // Applied to the form only: nothing saved yet; the weather topic now follows SQMeter, read-only.
  const before = await page.evaluate(async () => (await (await fetch('/api/config')).json()).mqtt.username);
  expect(before).not.toBe('observatory');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() =>
      page.evaluate(async () => {
        const config = await (await fetch('/api/config')).json();
        const sync = config.sqmeterSync;
        return `${config.mqtt.username}|${config.mqtt.topic}|${config.alerts.ntfy.topic}|${sync.mqtt.follow}|${sync.alerts.follow}`;
      }),
    )
    .toBe('observatory|roofroof|roofroof-demo|true|false');
});

// Spec 020: a group that follows SQMeter is read-only; "Edit here instead" unlocks it and Save keeps it set here.
test('Settings → Time follows SQMeter read-only until Edit here instead, which saves follow false', async ({ page }) => {
  await page.goto('./#/settings?tab=time');
  const server = page.locator('[data-field="ntp.server1"]');
  await expect(server).toBeDisabled({ timeout: 15_000 });
  await expect(page.locator('[data-sync="time"]').first()).toContainText('Follows SQMeter');
  await page.getByRole('button', { name: 'Edit here instead' }).first().click();
  await expect(server).toBeEnabled();
  await server.fill('time.example.org');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() =>
      page.evaluate(async () => {
        const config = await (await fetch('/api/config')).json();
        return `${config.sqmeterSync.time.follow}|${config.ntp.server1}`;
      }),
    )
    .toBe('false|time.example.org');
  await expect(page.getByRole('button', { name: 'Follow SQMeter' }).first()).toBeVisible();
});

// Spec 020: a password-protected SQMeter. Locked, the copy and the weather source say SQMeter wants a
// password; with its login entered in Settings and saved, the copy works and the weather recovers.
test('a locked SQMeter: the copy asks for its password; with the login saved it works again', async ({ page }) => {
  test.setTimeout(120_000);
  await page.goto('./#/settings?tab=roof&section=sqmeterCopy');
  const card = page.locator('#sqmeterCopy');
  await expect(card).toBeVisible({ timeout: 15_000 });
  const toggle = page.locator('.demo-panel-toggle');
  if ((await toggle.getAttribute('aria-expanded')) !== 'true') await toggle.click();
  await page.getByRole('button', { name: 'SQMeter password: On', exact: true }).click();
  await page.getByRole('button', { name: 'Clock speed 10x', exact: true }).click();
  await toggle.click();

  await card.getByRole('button', { name: 'Copy from SQMeter' }).click();
  const alert = card.getByRole('alert');
  await expect(alert).toContainText('SQMeter wants a password', { timeout: 15_000 });
  await expect(alert).toContainText("Enter or check SQMeter's password in the fields above");
  const weatherText = () => page.evaluate(async () => (await (await fetch('/api/status')).json()).roof.weatherText as string);
  await expect.poll(weatherText, { timeout: 60_000 }).toContain('SQMeter wants a password');

  // A wrong password is refused.
  const safety = page.locator('#safety');
  await safety.locator('[data-field="safety.http.username"]').fill('admin');
  await safety.locator('[data-field="safety.http.password"]').fill('wrong');
  await card.getByRole('button', { name: 'Copy from SQMeter' }).click();
  await expect(card.getByRole('alert')).toContainText('SQMeter refused the password', { timeout: 15_000 });

  await safety.locator('[data-field="safety.http.password"]').fill('sqmeter');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() => page.evaluate(async () => (await (await fetch('/api/config')).json()).safety.http))
    .toMatchObject({ username: 'admin', password: '********' });
  await card.getByRole('button', { name: 'Copy from SQMeter' }).click();
  await expect(card.getByRole('alert')).toHaveCount(0);
  await expect(card.getByRole('button', { name: 'Apply ticked' }).or(card.getByText('Nothing to copy'))).toBeVisible({ timeout: 15_000 });
  await expect.poll(weatherText, { timeout: 60_000 }).not.toContain('password');
  await expect
    .poll(() => page.evaluate(async () => (await (await fetch('/api/status')).json()).roof.weather), { timeout: 60_000 })
    .toBe('safe');
});

// Spec 024: SQMeter shares its passwords with its login; the copy brings the MQTT password across
// as a placeholder the device fills in, and no response ever carries the password itself.
test("SQMeter's passwords: why not without a web password; with its login, received and saved unseen", async ({ page }) => {
  test.setTimeout(120_000);
  const bodies: string[] = [];
  page.on('response', async (response) => {
    if (response.url().includes('/api/')) bodies.push(await response.text().catch(() => ''));
  });
  await page.goto('./#/settings?tab=roof&section=sqmeterCopy');
  const card = page.locator('#sqmeterCopy');
  await expect(card).toBeVisible({ timeout: 15_000 });
  await card.getByRole('button', { name: 'Copy from SQMeter' }).click();
  await expect(card).toContainText('SQMeter has no web password', { timeout: 15_000 });
  await expect(card).toContainText('Enter the MQTT password');
  await card.getByRole('button', { name: 'Cancel' }).click();

  const toggle = page.locator('.demo-panel-toggle');
  if ((await toggle.getAttribute('aria-expanded')) !== 'true') await toggle.click();
  await page.getByRole('button', { name: 'SQMeter password: On', exact: true }).click();
  await toggle.click();
  const safety = page.locator('#safety');
  await safety.locator('[data-field="safety.http.username"]').fill('admin');
  await safety.locator('[data-field="safety.http.password"]').fill('sqmeter');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() => page.evaluate(async () => (await (await fetch('/api/config')).json()).safety.http))
    .toMatchObject({ username: 'admin', password: '********' });

  await card.getByRole('button', { name: 'Copy from SQMeter' }).click();
  await expect(card).toContainText('Passwords received from SQMeter', { timeout: 15_000 });
  await expect(card).toContainText('unencrypted');
  await expect(card.getByRole('switch', { name: 'MQTT broker' })).toBeChecked();
  await expect(card).not.toContainText('Enter the MQTT password');
  await card.getByRole('button', { name: 'Apply ticked' }).click();
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() => page.evaluate(async () => (await (await fetch('/api/config')).json()).mqtt))
    .toMatchObject({ username: 'observatory', password: '********' });
  expect(bodies.some((body) => body.includes('demo-broker-pass'))).toBe(false);
});

// Spec 020 FR-036, FR-038: SQMeter's address only (a URL of another SQMeter page is taken as it),
// and Test reads the simulated SQMeter once, through the device's own code.
test("SQMeter's address: a full URL saves as the address, and Test reads SQMeter once", async ({ page }) => {
  await page.goto('./#/settings?tab=roof&section=safety');
  const safety = page.locator('#safety');
  const address = safety.locator('[data-field="safety.http.url"]');
  await expect(address).toBeVisible({ timeout: 15_000 });
  await expect(address).toHaveAttribute('placeholder', '192.168.1.50 or sqmeter.local');
  await address.fill('http://sqmeter.local/api/sensors');
  await safety.getByRole('button', { name: 'Test', exact: true }).click();
  await expect(safety.getByRole('status')).toContainText('SQMeter says', { timeout: 15_000 });
  await page.getByRole('button', { name: 'Save' }).click();
  await expect
    .poll(() => page.evaluate(async () => (await (await fetch('/api/config')).json()).safety.http.url))
    .toBe('http://sqmeter.local');

  // A locked SQMeter: Test says so.
  const toggle = page.locator('.demo-panel-toggle');
  if ((await toggle.getAttribute('aria-expanded')) !== 'true') await toggle.click();
  await page.getByRole('button', { name: 'SQMeter password: On', exact: true }).click();
  await toggle.click();
  await safety.getByRole('button', { name: 'Test', exact: true }).click();
  await expect(safety.getByRole('status')).toContainText('SQMeter wants a password', { timeout: 15_000 });
});
