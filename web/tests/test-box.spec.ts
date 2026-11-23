import { test, expect, type Page } from '@playwright/test';

// Spec 022: test box mode in the demo, which runs the device's own rules: the
// bypasses saved through the settings API, the red banner on every page, the
// dashboard's park and weather rows, and the checks back once they are off.

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
  const box = await testBox(page);
  expect(box).toMatchObject({ set: true, park: true, weather: true, daylight: false, expired: false });
  expect(box.until).toBeGreaterThan(Date.now() / 1000 + 23 * 3600);

  // The demo lives in this page: no reload (it would start the device again). The banner reads every 5 s.
  const banner = page.locator('.test-box-banner');
  await expect(banner).toContainText('park check (no telescope), weather check (no weather source) bypassed until', { timeout: 8000 });
  await expect(page.locator('[data-inventory="roof-card"]').getByText('Test box bypass').first()).toBeVisible();
  await page.getByRole('navigation').getByRole('button', { name: /System/ }).click();
  await expect(banner).toBeVisible();

  // Off again: the banner goes and the checks are back.
  expect((await post(page, '/api/config', { testBox: { park: false, weather: false } })).status).toBe(200);
  await expect.poll(async () => (await testBox(page)).set, { timeout: 5000 }).toBe(false);
  await expect(banner).toHaveCount(0, { timeout: 8000 });
});
