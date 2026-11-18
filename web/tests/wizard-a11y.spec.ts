import { test, expect } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';

// Spec 018 FR-022, SC-002: every wizard screen, and the dialog's own screens,
// pass the WCAG 2.2 AA scan with no violation and fit without sideways scroll,
// at 320 px and desktop, with the browser preferring light and dark. The
// device is set up (the demo's state), so every conditional screen shows.

const TAGS = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];
const SCREENS = [
  'name',
  'password',
  'site',
  'time',
  'supply',
  'current',
  'parkSwitches',
  'rain',
  'hallFitted',
  'hall',
  'calibrate',
  'motion',
  'weatherSource',
  'weatherAddress',
  'nina',
  'alpaca',
  'alertChannel',
  'alertDetails',
  'daylight',
  'review',
];
const VIEWPORTS = [
  { name: 'desktop', width: 1280, height: 800 },
  { name: 'phone', width: 320, height: 640 },
] as const;

for (const scheme of ['dark', 'light'] as const) {
  for (const viewport of VIEWPORTS) {
    test(`every wizard screen at ${viewport.name}, ${scheme}`, async ({ page }) => {
      test.setTimeout(180_000);
      await page.emulateMedia({ colorScheme: scheme });
      await page.setViewportSize(viewport);
      const failures: string[] = [];
      const check = async (where: string) => {
        const results = await new AxeBuilder({ page }).withTags(TAGS).analyze();
        for (const v of results.violations) failures.push(`${where}: ${v.id} (${v.nodes.map((n) => n.target.join(' ')).join(', ')})`);
        const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
        if (overflow > 1) failures.push(`${where}: ${overflow}px wider than the viewport`);
        if ((await page.locator('h1').count()) !== 1) failures.push(`${where}: not one h1`);
      };
      for (const id of SCREENS) {
        await page.goto(`./?panel=hidden#/wizard?screen=${id}`);
        await page.reload();
        await expect(page.locator('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id, { timeout: 15_000 });
        await page.waitForTimeout(300);
        await check(id);
      }
      // The dialog's own screens.
      await page.keyboard.press('Escape');
      await expect(page.locator('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', 'leave');
      await check('leave');
      await page.getByRole('button', { name: 'Skip setup' }).click();
      await check('skip');
      await page.getByRole('button', { name: 'Keep going' }).click();
      await page.getByRole('button', { name: 'Finish' }).click();
      await expect(page.locator('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', 'done');
      await check('done');
      expect(failures, failures.join('\n')).toEqual([]);
    });
  }
}
