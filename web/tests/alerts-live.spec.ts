import { test, expect, type Page } from '@playwright/test';

// Alerts without a reload: custom wording saves in any script, the reset asks
// first, and a new alert reaches an open page at once.

const openAlerts = async (page: Page) => {
  await page.goto('./#/settings?tab=alerts');
  await page.locator('[data-event]').first().waitFor();
};

const editWording = async (page: Page, event: string) => {
  await page.locator(`[data-event="${event}"] button`).last().click();
  return page.locator(`[data-template="${event}"]`);
};

const savedEvents = (page: Page) => page.evaluate(async () => (await (await fetch('/api/config')).json()).alerts.events);

test.describe('custom alert wording', () => {
  const samples: Record<string, string> = { latin: 'Está lloviendo', arabic: 'إنها تمطر', japanese: '雨'.repeat(80) };
  for (const [script, title] of Object.entries(samples)) {
    test(`saves ${script} text`, async ({ page }) => {
      await openAlerts(page);
      const editor = await editWording(page, 'client_lost');
      await editor.locator('input').fill(title);
      await expect(editor.locator('.field-error')).toHaveCount(0); // 80 characters, even at 3 bytes each
      await page.locator('.save-bar button').last().click();
      await expect.poll(async () => (await savedEvents(page)).client_lost.title).toBe(title);
    });
  }

  test('reset asks first and is not a primary button', async ({ page }) => {
    await openAlerts(page);
    const editor = await editWording(page, 'client_lost');
    await editor.locator('input').fill('My own title');
    const reset = editor.getByRole('button', { name: 'Reset' });
    await expect(reset).toHaveClass(/btn-link/);
    await reset.click();
    await editor.getByRole('button', { name: 'Cancel' }).click();
    await expect(editor.locator('input')).toHaveValue('My own title');
    await editor.getByRole('button', { name: 'Reset' }).click();
    await editor.getByRole('group').getByRole('button', { name: 'Reset' }).click();
    await expect(editor.locator('input')).toHaveValue('');
  });
});

test('a new alert reaches the bell without waiting for a poll', async ({ page }) => {
  await page.goto('./#/');
  await page.locator('main').waitFor();
  await page.waitForTimeout(1500);
  await page.evaluate(() => fetch('/api/alerts/test?event=client_lost&level=2', { method: 'POST' }));
  // The fallback poll is a minute; the pushed revision brings it in seconds.
  await expect(page.locator('.alerts-bell-count')).toBeVisible({ timeout: 5000 });
});
