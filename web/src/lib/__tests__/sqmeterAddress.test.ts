import { describe, expect, it } from 'vitest';
import { sqmeterAddress, sqmeterAddressError, sqmeterHttps } from '../sqmeterAddress';

// Spec 020 FR-036, FR-039: the same rules as the device (test/test_config_safety).
describe("SQMeter's address", () => {
  it('is kept as scheme, host and port', () => {
    expect(sqmeterAddress('192.168.4.144')).toBe('http://192.168.4.144');
    expect(sqmeterAddress('  sqmeter.local/ ')).toBe('http://sqmeter.local');
    expect(sqmeterAddress('http://192.168.4.144/api/sensors')).toBe('http://192.168.4.144');
    expect(sqmeterAddress('HTTP://192.168.4.144:8080/api/safety/')).toBe('http://192.168.4.144:8080');
    expect(sqmeterAddress('https://sqm.lan/api/status?x=1')).toBe('https://sqm.lan');
    expect(sqmeterAddress('http://sqm/API/Config')).toBe('http://sqm');
    expect(sqmeterAddress('[fd00::10]:8080/api/safety')).toBe('http://[fd00::10]:8080');
    expect(sqmeterAddress('   ')).toBe('');
    expect(sqmeterAddress('http://sqm/foo')).toBe('http://sqm/foo');
  });

  it('refuses another path, another scheme and a bad host; https is told apart', () => {
    expect(sqmeterAddressError('sqmeter.local')).toBeNull();
    expect(sqmeterAddressError('')).toBeNull();
    expect(sqmeterAddressError('http://sqm/weather')).toMatch(/address only/);
    expect(sqmeterAddressError('ftp://sqm')).not.toBeNull();
    expect(sqmeterAddressError('http://bad host')).not.toBeNull();
    expect(sqmeterHttps('https://sqm.lan')).toBe(true);
    expect(sqmeterHttps('sqm.lan')).toBe(false);
  });
});
