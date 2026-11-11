import { afterEach, describe, expect, it } from 'vitest';
import en from '../en.json';
import { setLanguage, type Messages } from '../index';
import { deviceText } from '../deviceMessage';
import { channelLabel, deliveryStatusLabel } from '../../lib/alertDelivery';

// RoofRoof ships English only (spec 001); a pseudo-language that brackets
// every message shows the device's text goes through the message catalogue.
const pseudo = Object.fromEntries(
  Object.entries(en as Record<string, unknown>).map(([key, text]) => [key, `⟦${text}⟧`]),
) as unknown as Messages;

describe('device text in the UI language', () => {
  afterEach(() => setLanguage('en', null));

  it('translates a test alert, values included', () => {
    setLanguage('xx', pseudo);
    expect(deviceText('Test: Test notification')).toBe('⟦Test: ⟦Test notification⟧⟧');
  });

  it('leaves text it does not know as sent', () => {
    setLanguage('xx', pseudo);
    expect(deviceText('My own title')).toBe('My own title');
    expect(deviceText('HTTP 401')).toBe('HTTP 401');
  });

  it('labels channels and delivery results', () => {
    setLanguage('xx', pseudo);
    expect(channelLabel('ntfy')).toBe('ntfy');
    expect(deliveryStatusLabel('sent')).toBe(`⟦${en['alerts.delivery.sent']}⟧`);
    expect(deliveryStatusLabel('pending')).toBe(`⟦${en['alerts.delivery.sending']}⟧`);
  });
});
