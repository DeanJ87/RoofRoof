import { test, expect } from '@playwright/test';
import { fileURLToPath } from 'url';
import path from 'path';
import fs from 'fs';

// This file's directory (ESM has no __dirname).
const HERE = path.dirname(fileURLToPath(import.meta.url));

const SCREENSHOTS_DIR = path.resolve(HERE, '../../docs/assets/screenshots');

fs.mkdirSync(SCREENSHOTS_DIR, { recursive: true });

const save = (name: string) => path.join(SCREENSHOTS_DIR, `${name}.png`);

const waitForDemoApp = async (page: import('@playwright/test').Page) => {
  await page.waitForLoadState('domcontentloaded');
  await page.evaluate(() => window.__mswReady);
};

// The dashboard re-lays out its cards as live data arrives (with a short
// transition); wait until card positions and sizes stop changing.
const waitForLayout = async (page: import('@playwright/test').Page) => {
  await page.evaluate(async () => {
    const snapshot = () =>
      Array.from(document.querySelectorAll('.masonry-item'))
        .map((el) => {
          const r = el.getBoundingClientRect();
          return `${Math.round(r.x)},${Math.round(r.y)},${Math.round(r.height)}`;
        })
        .join('|');
    let last = '';
    let stableSince = performance.now();
    const deadline = performance.now() + 8000;
    while (performance.now() < deadline) {
      await new Promise((resolve) => setTimeout(resolve, 100));
      const now = snapshot();
      if (now !== last) {
        last = now;
        stableSince = performance.now();
      } else if (performance.now() - stableSince > 700) {
        return;
      }
    }
  });
};

const capturePage = async (page: import('@playwright/test').Page, name: string) => {
  await waitForLayout(page);
  await page.screenshot({ path: save(name), fullPage: true });
};

// The demo uses hash routing so GitHub Pages hard-refreshes never 404.
// All goto() calls use "./" or "./#/route" — resolved against the Playwright
// baseURL (http://localhost:4173/ unless PW_PORT says otherwise) so they reach the demo server.

test.beforeEach(async ({ page }) => {
  // Give MSW service worker time to activate before each test
  await page.addInitScript(() => {
    window.__mswReady = new Promise<void>((resolve) => {
      navigator.serviceWorker.ready.then(() => resolve());
    });
  });
});

test('dashboard', async ({ page }) => {
  await page.goto('./?panel=hidden');
  await waitForDemoApp(page);
  await expect(page.getByRole('heading', { name: 'Device & Network' })).toBeVisible();
  await expect(page.locator('[data-inventory="alerts-state"]')).toContainText('Sending');
  await capturePage(page, 'dashboard');
});

test('status card', async ({ page }) => {
  await page.goto('./?panel=hidden');
  await waitForDemoApp(page);
  const card = page.locator('[data-inventory="status-card"]');
  await expect(card.locator('[data-inventory="alerts-state"]')).toContainText('Sending');
  await waitForLayout(page);
  await card.screenshot({ path: save('status-card') });
});

// Spec 009: the Roof card on its own, for the dashboard guide and the owner's review.
test('roof card', async ({ page }) => {
  await page.goto('./?panel=hidden');
  await waitForDemoApp(page);
  const card = page.locator('[data-inventory="roof-card"]');
  await expect(card.getByRole('button', { name: 'Stop', exact: true })).toBeVisible();
  await waitForLayout(page);
  await card.screenshot({ path: save('roof-card') });
});

test('system', async ({ page }) => {
  await page.goto('./?panel=hidden#/system');
  await waitForDemoApp(page);
  await expect(page.getByRole('heading', { name: 'Firmware' })).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Runtime Status' })).toBeVisible();
  await capturePage(page, 'system');
});

// Spec 017: the Diagnostics page during a move (switched on through the API; off by default).
test('diagnostics', async ({ page }) => {
  await page.goto('./?panel=hidden#/system');
  await waitForDemoApp(page);
  await page.waitForTimeout(2500); // the emulated device starts
  await page.evaluate(async () => {
    const body = JSON.stringify({ diag: { enabled: true } });
    await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
    await fetch('/api/roof/open', { method: 'POST', headers: { 'X-RoofRoof': '1' } });
    location.hash = '#/diagnostics';
  });
  await expect(page.getByRole('heading', { name: /in progress$/ })).toBeVisible({ timeout: 10_000 });
  await page.waitForTimeout(8000); // a few seconds of the move on the chart
  await page.screenshot({ path: save('diagnostics'), fullPage: true });
});

test('updates', async ({ page }) => {
  await page.goto('./?panel=hidden#/updates');
  await waitForDemoApp(page);
  await expect(page.getByRole('heading', { name: 'Manual upload' })).toBeVisible();
  await capturePage(page, 'updates');
});

test('alpaca', async ({ page }) => {
  await page.goto('./?panel=hidden#/alpaca');
  await waitForDemoApp(page);
  await expect(page.getByRole('heading', { name: 'ASCOM Alpaca' })).toBeVisible();
  await capturePage(page, 'alpaca');
});

for (const [id, label] of [
  ['device', 'Device'],
  ['roof', 'Roof'],
  ['network', 'Network'],
  ['time', 'Time'],
  ['alerts', 'Alerts'],
] as const) {
  test(`settings ${label}`, async ({ page }) => {
    await page.goto(`./?panel=hidden#/settings?tab=${id}`);
    await waitForDemoApp(page);
    const tab = page.getByRole('tab', { name: label });
    await tab.click();
    await expect(tab).toHaveAttribute('aria-selected', 'true');
    await capturePage(page, `settings-${id}`);
  });
}

test('alerts flyout', async ({ page }) => {
  await page.goto('./?panel=hidden');
  await waitForDemoApp(page);
  await waitForLayout(page);
  await page.getByRole('button', { name: /^Alerts/ }).click();
  const flyout = page.getByRole('dialog', { name: 'Recent alerts' });
  await expect(flyout).toBeVisible();
  await waitForLayout(page);
  await page.screenshot({ path: save('alerts-flyout') });
});

test('wifi setup', async ({ page }) => {
  await page.goto('./?panel=hidden#/wifi');
  await waitForDemoApp(page);
  await expect(page.getByRole('heading', { name: 'WiFi setup' })).toBeVisible();
  await capturePage(page, 'wifi-setup');
});

test('demo panel', async ({ page }) => {
  await page.goto('./');
  await waitForDemoApp(page);
  await waitForLayout(page);
  const skip = page.getByRole('button', { name: /^Skip/ });
  if (await skip.isVisible().catch(() => false)) await skip.click(); // the tour offer
  await page.getByRole('button', { name: /Demo/ }).last().click();
  await expect(page.getByRole('region', { name: 'Demo controls' })).toBeVisible();
  await page.screenshot({ path: save('demo-panel') });
});

// Specs 015 and 018: the setup wizard (the demo device is set up already; these open its screens directly).
const wizardShot = async (page: import('@playwright/test').Page, screen: string, name: string) => {
  await page.goto(`./?panel=hidden#/wizard?screen=${screen}`);
  await waitForDemoApp(page);
  await expect(page.locator('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', screen);
  if (screen === 'nina') {
    await page.getByRole('button', { name: 'Test', exact: true }).click();
    await expect(page.locator('[data-test-result="nina"]')).toBeVisible({ timeout: 20_000 });
  }
  await page.waitForTimeout(1500); // the live readings
  await page.screenshot({ path: save(name) });
};

for (const [screen, name] of [
  ['parkSwitches', 'wizard-choice'],
  ['supply', 'wizard-supply'],
  ['nina', 'wizard-nina'],
  ['review', 'wizard-review'],
] as const) {
  test(`setup wizard: ${screen}`, async ({ page }) => wizardShot(page, screen, name));
}

test('setup wizard at phone width', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await wizardShot(page, 'daylight', 'wizard-phone');
});

// Spec 016: the calibration page with a finished run in the demo is too slow to wait for; its start form.
test('calibration page', async ({ page }) => {
  await page.goto('./?panel=hidden#/calibrate');
  await waitForDemoApp(page);
  await expect(page.getByRole('button', { name: 'Start calibration' })).toBeVisible();
  await page.waitForTimeout(1500);
  await page.screenshot({ path: save('calibrate'), fullPage: true });
});
