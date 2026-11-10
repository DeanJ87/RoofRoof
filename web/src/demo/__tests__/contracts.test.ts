// @vitest-environment node
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import Ajv from 'ajv';
import { demoDevice } from '../device';
import { statusDocument } from '../handlers';
import { mockWifiNetworks } from '../../mocks/data';

// Every document the demo answers must match the device's contract
// (specs/001-platform-bootstrap/contracts/schemas, generated from a real
// SQMeter, cut down to RoofRoof's documents). A field the device doesn't send, or a missing one, fails here.

const files = import.meta.glob('../../../../specs/001-platform-bootstrap/contracts/schemas/*.schema.json', {
  eager: true,
  import: 'default',
});
const ajv = new Ajv({ allErrors: true, strict: false });
const schemas: Record<string, object> = Object.fromEntries(
  Object.entries(files).map(([path, schema]) => [path.split('/').pop()!.replace('.schema.json', ''), schema as object]),
);

const check = (schemaName: string, document: unknown) => {
  const validate = ajv.compile(schemas[schemaName]);
  const ok = validate(document);
  expect(ok, `${schemaName}: ${ajv.errorsText(validate.errors, { separator: '\n' })}`).toBe(true);
};

const alpaca = (path: string) => JSON.parse(demoDevice.alpaca('GET', path, []).body);

describe('demo documents match the device contract', () => {
  beforeAll(async () => {
    await demoDevice.start();
    await new Promise((resolve) => setTimeout(resolve, 2100)); // a couple of ticks
  });
  afterAll(() => demoDevice.stop());

  it('status (/api/status, /ws/status)', () => check('status', statusDocument()));
  it('settings in effect (SQMeter spec 020)', () => check('settings-effective', JSON.parse(demoDevice.effective())));
  it('config', () => check('config', JSON.parse(demoDevice.getConfig())));
  it('recent alerts', () => check('alerts-recent', JSON.parse(demoDevice.recentAlerts())));
  it('alerts sending or paused (/api/alerts/armed)', () => check('alerts-armed', JSON.parse(demoDevice.armedDocument())));
  it('WiFi scan (SQMeter spec 028 FR-002)', () => {
    check('wifi-scan', { success: true, scanning: false, networks: mockWifiNetworks });
  });
  it('Alpaca management and device state', () => {
    check('alpaca-description', alpaca('/management/v1/description'));
    check('alpaca-configured-devices', alpaca('/management/v1/configureddevices'));
  });

  it('a field the device does not send fails the check', () => {
    const status = statusDocument() as Record<string, unknown>;
    status.upTime = status.uptime; // e.g. a rename that drifted
    const validate = ajv.compile(schemas.status);
    expect(validate(status)).toBe(false);
  });
});
