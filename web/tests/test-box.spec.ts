import { test, expect, type Page } from '@playwright/test';

// Spec 022: test box mode in the demo, which runs the device's own rules: the
// bypasses saved through the settings API, the red banner on every page, the
// dashboard's park and weather rows, the calibrate checklists, the test box
// page, and the checks back once it is turned off.

const api = (page: Page, path: string, init?: RequestInit) =>
  page.evaluate(
    async ([p, i]) => {
      const response = await fetch(p as string, i as RequestInit | undefined);
      return { status: response.status, body: await response.text() };
    },
    [path, init] as const,
  );

const post = (page: Page, path: string, body: unknown) =>
  api(page, path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });

const testBox = async (page: Page) => JSON.parse((await api(page, '/api/status')).body).roof.testBox;

test('a test box: bypasses in effect, shown on every page, and off again', async ({ page }) => {
  await page.goto('./');
  await expect(page.locator('main')).toBeVisible();
  await page.waitForTimeout(2500);

  // Refused beside the source it stands in for.
  const refused = await post(page, '/api/config', { testBox: { park: true }, park: { nina: { url: 'http://10.0.0.2:1888' } } });
  expect(refused.status).toBe(400);
  expect(refused.body).toContain('clear the N.I.N.A. address first');

  const saved = await post(page, '/api/config', {
    park: { nina: { url: '' } },
    safety: { http: { enabled: false }, mqtt: { enabled: false } },
    testBox: { park: true, weather: true, hours: 24 },
  });
  expect(saved.status).toBe(200);
  await expect.poll(async () => (await testBox(page)).active, { timeout: 5000 }).toBe(true);
  // As a save from this page does: the banner reads the status at once.
  await page.evaluate(() => window.dispatchEvent(new Event('roofroof:config-saved')));
  const box = await testBox(page);
  expect(box).toMatchObject({ set: true, park: true, weather: true, daylight: false, expired: false });
  expect(box.until).toBeGreaterThan(Date.now() / 1000 + 23 * 3600);

  // The demo lives in this page: no reload (it would start the device again). The banner reads every 5 s.
  const banner = page.locator('.test-box-banner');
  await expect(banner.locator('.banner-title')).toHaveText('Test box: park check and weather check bypassed', { timeout: 8000 });
  await expect(banner.locator('.banner-detail')).toContainText('Faults still stop the roof.');
  await expect(page.locator('[data-inventory="roof-card"]').getByText('Test box bypass').first()).toBeVisible();
  await page
    .getByRole('navigation')
    .getByRole('button', { name: /System/ })
    .click();
  await expect(banner).toBeVisible();

  // Calibrate: the shared "why it can't move" list never says the mount is parked or the weather safe:
  // it names the bypassed checks (spec 026).
  await page.evaluate(() => (window.location.hash = '/calibrate'));
  const checks = page.locator('#calibrate-blockers [data-move-blockers]');
  await expect(checks).toContainText('Bypassed by the test box: Park check, Weather check.');
  await expect(checks).not.toContainText('Mount parked');

  // Manage opens the test box page; Turn off ends it.
  await banner.getByRole('button', { name: 'Manage test box' }).click();
  await expect(page.locator('[data-test-box-mode="on"]')).toBeVisible();
  await expect(banner.getByRole('button', { name: 'Manage test box' })).toHaveCount(0);
  await expect(page.locator('[data-test-box-check="park"]')).toContainText('Bypassed');
  await page.getByRole('button', { name: 'Turn off test box' }).click();
  await expect(page.getByText('Test box off: every check is back.')).toBeVisible();
  await expect.poll(async () => (await testBox(page)).set, { timeout: 5000 }).toBe(false);
  await expect(banner).toHaveCount(0, { timeout: 8000 });
  await expect(page.locator('[data-test-box-mode="off"]')).toBeVisible({ timeout: 12_000 });
});

// The banner sits in the page's own column, with room above the page's title,
// at desktop and phone width (DS-14).
for (const [name, viewport] of [
  ['desktop', { width: 1280, height: 900 }],
  ['phone', { width: 390, height: 844 }],
] as const) {
  test(`the banner lines up with the page at ${name} width`, async ({ page }) => {
    await page.setViewportSize(viewport);
    await page.goto('./');
    await expect(page.locator('main')).toBeVisible();
    await page.waitForTimeout(2500);
    expect((await post(page, '/api/config', { park: { nina: { url: '' } }, testBox: { park: true, hours: 24 } })).status).toBe(200);
    await expect.poll(async () => (await testBox(page)).active, { timeout: 5000 }).toBe(true);
    await page.evaluate(() => window.dispatchEvent(new Event('roofroof:config-saved')));
    const banner = page.locator('.test-box-banner');
    await expect(banner).toBeVisible({ timeout: 8000 });
    for (const [route, column, heading] of [
      ['/calibrate', '[data-calibrate-page]', '[data-calibrate-page] > .page-title'],
      ['/settings?tab=roof', '.settings-page', '.settings-tabs'],
      ['/', '.dashboard', '.dashboard'],
    ] as const) {
      await page.evaluate((r) => (window.location.hash = r), route);
      await expect(page.locator(column)).toBeVisible();
      const b = (await banner.boundingBox())!;
      const c = (await page.locator(column).first().boundingBox())!;
      const h = (await page.locator(heading).first().boundingBox())!;
      expect(Math.abs(b.x - c.x), `${route}: left edge`).toBeLessThan(1.5);
      expect(Math.abs(b.x + b.width - (c.x + c.width)), `${route}: right edge`).toBeLessThan(1.5);
      expect(h.y - (b.y + b.height), `${route}: space under the banner`).toBeGreaterThanOrEqual(10);
      expect(b.x, `${route}: inside the window`).toBeGreaterThanOrEqual(8);
      expect(b.x + b.width).toBeLessThanOrEqual(viewport.width - 8);
    }
    expect((await post(page, '/api/config', { testBox: { park: false } })).status).toBe(200);
  });
}
