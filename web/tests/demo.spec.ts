import { test, expect, type Page } from '@playwright/test';

// The demo as one emulated device (specs/016-demo-device-emulation).

const ready = async (page: Page, hash = '') => {
  await page.goto(`./${hash}`);
  await expect(page.locator('main')).toBeVisible();
  await page.waitForTimeout(2500); // first ticks of the emulated device
};

const api = (page: Page, path: string, init?: RequestInit) =>
  page.evaluate(
    async ([p, i]) => {
      const response = await fetch(p as string, i as RequestInit | undefined);
      return { status: response.status, body: await response.text() };
    },
    [path, init] as const,
  );

test.describe('demo links (US1, SC-001)', () => {
  test('every page renders and every link stays in the demo', async ({ page }) => {
    for (const hash of ['', '#/alpaca', '#/system', '#/settings', '#/updates', '#/wifi']) {
      await ready(page, hash);
      await expect(page.getByText('Page not found')).toHaveCount(0);
    }
    await ready(page, '#/alpaca');
    const links = await page.$$eval('main a[href]', (as) => as.map((a) => (a as HTMLAnchorElement).href));
    expect(links.length).toBeGreaterThan(1);
    for (const href of links) {
      const url = new URL(href);
      expect(url.origin).toBe(new URL(page.url()).origin);
      await page.goto(href);
      await page.waitForTimeout(1500);
      const text = await page.locator('body').innerText();
      expect(text).not.toContain("There isn't a GitHub Pages site here");
      expect(text).not.toContain('Page not found');
    }
  });

  test("device URLs opened directly show the device's answer", async ({ page }) => {
    await page.goto('./management/v1/description');
    // ServerName is the device's name (spec 007 FR-008).
    await expect(page.getByText('"ServerName": "RoofRoof Demo"')).toBeVisible();
    await expect(page.getByText('"Manufacturer": "RoofRoof"')).toBeVisible();
    await page.goto('./management/v1/configureddevices');
    await expect(page.getByText('"Value": []')).toBeVisible(); // no Alpaca device until spec 006
  });
});

test('settings change the emulated device (US2, SC-003)', async ({ page }) => {
  await ready(page);
  // Rejected with the device's own message.
  const bad = await api(page, '/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{"wifi":{"hostname":"-x"}}',
  });
  expect(bad.status).toBe(400);
  expect(JSON.parse(bad.body).error).toContain('Hostname');

  // Alpaca off: the status and the Alpaca list say so.
  await api(page, '/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"alpaca":{"enabled":false}}' });
  await page.waitForTimeout(1500);
  expect(JSON.parse((await api(page, '/api/status')).body).alpaca.enabled).toBe(false);

  // A restart keeps what was saved, like the device.
  await api(page, '/api/restart', { method: 'POST' });
  await page.waitForTimeout(6500);
  expect(JSON.parse((await api(page, '/api/config')).body).alpaca.enabled).toBe(false);
});

test('nothing leaves the browser (US3, SC-004)', async ({ page, baseURL }) => {
  const outside: string[] = [];
  const own = new URL(baseURL ?? 'http://localhost:4173/').origin;
  page.on('request', (request) => {
    const url = new URL(request.url());
    if (url.protocol.startsWith('http') && url.origin !== own) outside.push(request.url());
  });
  await ready(page);
  const post = (path: string, body?: string) => api(page, path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
  await post(
    '/api/config',
    '{"alerts":{"enabled":true,"pushover":{"enabled":true,"userKey":"u12345678901234567890123456789","appToken":"a12345678901234567890123456789"},"webhook":{"enabled":true,"url":"https://example.com/hook"}}}',
  );
  await post('/api/alerts/test?channel=all');
  await post('/api/alerts/test?channel=pushover&event=client_lost&level=4');
  await post('/api/mqtt/test', '{"broker":"test.mosquitto.org","port":1883}');
  await api(page, '/api/wifi/scan');
  await post('/api/wifi/connect', '{"ssid":"x","password":"y"}');
  for (const hash of ['#/settings?tab=alerts', '#/updates', '#/wifi', '#/system']) await ready(page, hash);
  const recent = JSON.parse((await api(page, '/api/alerts/recent')).body);
  expect(recent.alerts[0].channels.pushover.detail).toBe('Demo: nothing was sent');
  // The API explorer (specs/028-openapi) too: loading it and trying a request.
  await page.goto('./explorer/');
  await page
    .getByRole('button', { name: /Test Request.*get \/api\/status/i })
    .first()
    .click();
  await page.getByRole('button', { name: /^Send get request to .*\/api\/status$/ }).click();
  await page.waitForTimeout(1000);
  expect(outside).toEqual([]);
});

// The demo's status looks like a real device's.
test('demo status is plausible', async ({ page }) => {
  await ready(page);
  const status = JSON.parse((await api(page, '/api/status')).body);
  const within = (value: number, min: number, max: number, what: string) => {
    expect(value, what).toBeGreaterThanOrEqual(min);
    expect(value, what).toBeLessThanOrEqual(max);
  };
  within(status.uptime, 0, 30 * 24 * 3600, 'uptime');
  within(status.wifi.rssi, -100, -20, 'WiFi signal');
  expect(status.alpaca.enabled).toBe(true);
});
