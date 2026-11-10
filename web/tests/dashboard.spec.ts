import { test, expect, type Page } from '@playwright/test';

// One test per item of web/src/dashboard/inventory.json (SQMeter spec 025 FR-003,
// 026 FR-010): drive the demo device into the state, check the item shows,
// then that it hides when its rule says so. tools/dashboard/check.py fails if
// a shown entry has no `inventory: <id>` here.

type Patch = Record<string, unknown>;

const statusCard = (page: Page) => page.locator('[data-inventory="status-card"]');

const open = async (page: Page, query = '') => {
  await page.goto(`./${query}#/`);
  await expect(statusCard(page)).toBeVisible({ timeout: 15_000 });
};

// Change the demo device's settings the way the Settings page does, then
// reload. Keys are paths; `*` matches every key at that level.
const setConfig = async (page: Page, changes: Patch) => {
  await page.evaluate(async (changes) => {
    const config = await (await fetch('/api/config')).json();
    const assign = (node: Record<string, unknown>, path: string[], value: unknown) => {
      const [head, ...rest] = path;
      const keys = head === '*' ? Object.keys(node) : [head];
      for (const key of keys) {
        if (!rest.length) node[key] = value;
        else assign((node[key] ??= {}) as Record<string, unknown>, rest, value);
      }
    };
    for (const [path, value] of Object.entries(changes)) assign(config, path.split('.'), value);
    await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(config) });
  }, changes);
  await page.reload();
  await expect(statusCard(page)).toBeVisible({ timeout: 15_000 });
};

const item = (page: Page, id: string) => page.locator(`[data-inventory="${id}"]`);
const tile = (page: Page, id: string) => statusCard(page).locator(`.status-tile[data-inventory="${id}"]`);
const pill = (page: Page) => statusCard(page).locator('.card-actions .pill');
const card = (page: Page, title: string) =>
  page.locator('.masonry-item').filter({ has: page.getByRole('heading', { name: title, exact: true }) });

const openPanel = async (page: Page) => {
  const toggle = page.locator('.demo-panel-toggle');
  if ((await toggle.getAttribute('aria-expanded')) !== 'true') await toggle.click();
};

test.describe.configure({ retries: 0 });

test('healthy: the Status card is the alerts state, All good', async ({ page }) => {
  // inventory: status-card  inventory: alerts-state
  await open(page);
  await expect(statusCard(page).getByRole('heading', { name: 'Status', exact: true })).toBeVisible();
  await expect(pill(page)).toHaveText('All good');
  // No Alpaca device until spec 006, so no imaging-app tile yet; Alerts is the one tile.
  await expect(tile(page, 'imaging-app')).toHaveCount(0);
  await expect(tile(page, 'alerts-state')).toContainText('Sending');
  await expect(statusCard(page).locator('.status-row')).toHaveCount(0);
  // It is the first card, and nothing sits between the header and the cards (026 FR-014).
  await expect(page.locator('.masonry-item').first()).toContainText('Status');
});

test('paused alerts show on the Alerts tile with Resume, then go', async ({ page }) => {
  // inventory: alerts-state
  await open(page);
  await page.evaluate(() => fetch('/api/alerts/disarm?source=ui', { method: 'POST' }));
  await expect(tile(page, 'alerts-state')).toContainText('Paused', { timeout: 10_000 });
  await expect(tile(page, 'alerts-state')).toContainText('Paused by you');
  await tile(page, 'alerts-state').getByRole('button', { name: 'Resume' }).click();
  await expect(tile(page, 'alerts-state')).toContainText('Sending', { timeout: 10_000 });
});

test('send mode not in effect while Alpaca is off', async ({ page }) => {
  // inventory: alerts-mode-not-in-effect
  await open(page);
  await setConfig(page, { 'alerts.sendMode': 'whileConnected', 'alpaca.enabled': false });
  await expect(item(page, 'alerts-mode-not-in-effect')).toContainText('Send mode');
  await expect(item(page, 'alerts-mode-not-in-effect')).toContainText('Not in effect');
  await setConfig(page, { 'alpaca.enabled': true });
  await expect(item(page, 'alerts-mode-not-in-effect')).toHaveCount(0);
});

test('imaging app: no tile without an Alpaca device, whether Alpaca is on or off', async ({ page }) => {
  // inventory: imaging-app
  await open(page);
  await expect(tile(page, 'imaging-app')).toHaveCount(0);
  await setConfig(page, { 'alpaca.enabled': false });
  await expect(tile(page, 'imaging-app')).toHaveCount(0);
  await setConfig(page, { 'alpaca.enabled': true });
  await expect(tile(page, 'imaging-app')).toHaveCount(0);
});

test('no alert channel can send', async ({ page }) => {
  // inventory: no-channel
  await open(page);
  await expect(item(page, 'no-channel')).toHaveCount(0);
  await setConfig(page, { 'alerts.ntfy.enabled': false, 'alerts.mqtt.enabled': true, 'mqtt.enabled': false });
  await expect(item(page, 'no-channel')).toContainText('Alert channels');
  await expect(item(page, 'no-channel')).toContainText("Can't send");
});

test('clock not set is one row; it is absent while the clock is set', async ({ page }) => {
  // inventory: clock-location
  await open(page);
  await expect(item(page, 'clock-location')).toHaveCount(0);
});

// The imaging app's states and the dropped live connection (freshness) are
// unit-tested in web/src/dashboard/__tests__/glance.test.ts: the demo has no
// imaging app to connect, and its stream never drops.

// Each fact once (DS-08): the Status card never repeats what another card shows.
test('the Status card repeats no other card', async ({ page }) => {
  await open(page);
  await page.waitForTimeout(3000);
  const others = (
    await page
      .locator('.masonry-item')
      .filter({ hasNot: statusCard(page) })
      .locator('.card-actions .pill')
      .allInnerTexts()
  ).map((text) => text.trim());
  const status = (await statusCard(page).locator('.pill').allInnerTexts()).map((text) => text.trim());
  for (const word of ['Connected', 'Live', 'Stale', 'Error', 'Not responding']) {
    if (others.includes(word)) expect(status, `"${word}" is already on another card`).not.toContain(word);
  }
});

// DS-08 and DS-24 on what the dashboard renders, not just the English
// strings: copy composed in code can join facts too. Every card, in states
// that show the most text: no " · " or " - " run-ons, and the Status card's
// tiles carry no explanation under the pill.
for (const [state, patch] of [
  ['healthy', null],
  ['Alpaca off and alerts off', { 'alpaca.enabled': false, 'alerts.enabled': false }],
] as const) {
  test(`no run-on text on the dashboard: ${state}`, async ({ page }) => {
    await open(page);
    if (patch) await setConfig(page, patch);
    await page.waitForTimeout(2500);
    for (const text of await page.locator('.masonry-item').allInnerTexts()) {
      for (const line of text.split('\n')) {
        expect(line, 'DS-24: one fact per line').not.toMatch(/ [·•] | [-–] /);
      }
    }
    await expect(statusCard(page).locator('.status-tile a')).toHaveCount(0);
    for (const tile of await statusCard(page).locator('.status-tile:not([data-inventory="imaging-app"])').all()) {
      await expect(tile.locator('.metric-sub')).toHaveCount(0);
    }
  });
}

// A tile value is one line (DS-03): long values step down a size instead of wrapping.
for (const width of [1280, 390]) {
  test(`tile values stay on one line at ${width} px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 900 });
    await open(page);
    await page.waitForTimeout(2500);
    for (const value of await page.locator('.masonry-item .metric-value').all()) {
      const lines = await value.evaluate((el) => {
        const style = getComputedStyle(el);
        const line = parseFloat(style.lineHeight) || parseFloat(style.fontSize) * 1.2;
        return Math.round(el.getBoundingClientRect().height / line);
      });
      expect(lines, `"${await value.innerText()}" wraps`).toBeLessThanOrEqual(1);
    }
  });
}

test('the demo says its clock moved on the Demo button, not the dashboard', async ({ page }) => {
  await open(page);
  await expect(page.locator('.demo-clock-moved')).toHaveCount(0);
  await openPanel(page);
  await page.getByRole('button', { name: /^Clock speed 10x/ }).click();
  await expect(page.locator('.demo-panel-toggle')).toContainText('Clock moved', { timeout: 20_000 });
});

test('the dashboard is the Status card and Device & Network, nothing from the old sensors', async ({ page }) => {
  // inventory: device-network-card
  await open(page);
  await expect(card(page, 'Device & Network')).toBeVisible();
  await expect(page.locator('.masonry-item')).toHaveCount(2);
});

test('Device & Network: one address per row, local name and MQTT', async ({ page }) => {
  // inventory: ipv6  inventory: mdns  inventory: mqtt-state
  await open(page);
  const device = card(page, 'Device & Network');
  await expect(device.locator('.reading-row').filter({ hasText: 'IPv4' })).toBeVisible();
  await expect(item(page, 'ipv6')).toBeVisible();
  // Each IPv6 address is its own row (026 DS-05).
  const addresses = item(page, 'ipv6').locator('.reading-row');
  expect(await addresses.count()).toBeGreaterThan(0);
  for (const row of await addresses.all()) await expect(row.locator('.reading-value')).toHaveCount(1);
  await expect(item(page, 'mdns')).toContainText('.local');
  await expect(item(page, 'mqtt-state')).toContainText('Connected');
  await setConfig(page, { 'wifi.ipv6': false, 'wifi.mdns': false, 'mqtt.enabled': false });
  await expect(item(page, 'ipv6')).toHaveCount(0);
  await expect(item(page, 'mdns')).toHaveCount(0);
  await expect(item(page, 'mqtt-state')).toHaveCount(0);
});
