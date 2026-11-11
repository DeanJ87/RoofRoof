// Every page and state the accessibility checks cover (SQMeter spec 022 FR-001,
// FR-003). The demo build runs the device's own components, so a result here
// holds for the device; the captive-portal setup page is the /wifi route.
//
// Add new routes and dialogs here: tests/a11y.spec.ts checks each entry in
// each of its states, at both viewports, against tests/a11y/baseline.json.

export type A11yState = 'default' | 'dialog' | 'demo-panel' | 'error';

export interface InventoryEntry {
  id: string; // stable key used in the baseline
  route: string; // hash route
  component: string; // owner, for findings
  states: A11yState[];
}

export const VIEWPORTS = [
  { name: 'desktop', width: 1280, height: 800 },
  { name: 'phone', width: 320, height: 640 }, // the 400% reflow case
] as const;

const settingsTab = (tab: string, component: string, extra: A11yState[] = []): InventoryEntry => ({
  id: `settings-${tab}`,
  route: `#/settings?tab=${tab}`,
  component,
  states: ['default', ...extra],
});

export const INVENTORY: InventoryEntry[] = [
  { id: 'dashboard', route: '#/', component: 'Dashboard', states: ['default', 'dialog', 'demo-panel'] },
  { id: 'alpaca', route: '#/alpaca', component: 'Alpaca', states: ['default'] },
  { id: 'system', route: '#/system', component: 'System', states: ['default'] },
  settingsTab('device', 'settings/DeviceTab', ['error']),
  settingsTab('roof', 'settings/RoofTab'),
  settingsTab('network', 'settings/NetworkTab'),
  settingsTab('time', 'settings/TimeTab'),
  settingsTab('alerts', 'settings/AlertsTab'),
  { id: 'updates', route: '#/updates', component: 'Updates', states: ['default'] },
  { id: 'wifi', route: '#/wifi', component: 'WifiSetup (captive portal)', states: ['default'] },
  { id: 'not-found', route: '#/no-such-page', component: 'NotFound', states: ['default'] },
  // Spec 015: every page of the setup wizard.
  ...Array.from({ length: 10 }, (_, i) => ({
    id: `wizard-${i + 1}`,
    route: `#/wizard?step=${i + 1}`,
    component: 'Wizard',
    states: ['default'] as A11yState[],
  })),
];
