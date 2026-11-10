import { describe, expect, it } from 'vitest';
import { uploadOutcome } from '../updates';

// The device refuses another product's image before writing it (spec 001
// FR, lib/FirmwareImage verdictMessage); the Updates page shows its reason.
describe('uploadOutcome', () => {
  it.each(['Not RoofRoof firmware.', 'This firmware is for another product.'])('shows the refusal "%s"', (reason) => {
    expect(uploadOutcome(400, JSON.stringify({ error: reason }), 'Unknown error')).toEqual({ ok: false, errorMsg: reason });
  });

  it('takes a 200 with success as the device restarting onto the image', () => {
    expect(uploadOutcome(200, '{"success":true}', 'Unknown error')).toEqual({ ok: true });
  });

  it('falls back to the HTTP status when the reply is not JSON', () => {
    expect(uploadOutcome(500, 'oops', 'Unknown error')).toEqual({ ok: false, errorMsg: 'HTTP 500' });
  });
});
