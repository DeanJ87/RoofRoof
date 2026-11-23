// @vitest-environment jsdom
import { beforeEach, describe, expect, it } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fetchPins, fetchWiringCheck, startWiringCheck, validatePins } from '../pinsApi';
import { checkBlocked, hasEthernet, pinsRestartNeeded, suggestedPreset, withPreset } from '../pinsView';
import { DEVKIT_PINS, WT32_PINS, mockWiring, mockWiringDone, pinsDocumentFor } from '../../mocks/pins';
import { mockStatus } from '../../mocks/data';
import { server } from '../../test/mswServer';
// The mock device (MSW) and a page origin for the relative routes, as the components' tests have.
import '../../test/setup';

// Spec 023 (contracts/pins-api.md): the pin routes as the web UI reads them,
// and what the pages work out from them.

describe('pinsApi', () => {
  beforeEach(() => {
    mockWiring.state = { state: 'idle', checks: [] };
  });

  it('reads GET /api/pins; null when the device answers an error or is unreachable', async () => {
    expect((await fetchPins())?.running).toEqual(DEVKIT_PINS);
    server.use(http.get('/api/pins', () => HttpResponse.json({ error: 'nope' }, { status: 500 })));
    expect(await fetchPins()).toBeNull();
    server.use(http.get('/api/pins', () => HttpResponse.error()));
    expect(await fetchPins()).toBeNull();
  });

  it('validates a pin set: the device errors and warnings, a refusal, or unreachable', async () => {
    const checked = await validatePins({ ...DEVKIT_PINS, preset: 'custom', rpwm: 35, lpwm: 15 });
    expect(checked.kind).toBe('checked');
    if (checked.kind !== 'checked') return;
    expect(checked.validation.valid).toBe(false);
    expect(checked.validation.issues.map((issue) => [issue.function, issue.error])).toEqual([
      ['rpwm', true],
      ['lpwm', false],
      ['currentL', true],
    ]);
    server.use(http.post('/api/pins/validate', () => HttpResponse.json({ error: 'A pin map must be a JSON object' }, { status: 400 })));
    expect(await validatePins({})).toEqual({ kind: 'refused', text: 'A pin map must be a JSON object' });
    server.use(http.post('/api/pins/validate', () => HttpResponse.error()));
    expect(await validatePins({})).toEqual({ kind: 'unreachable' });
  });

  it('sends the pin map as JSON to validate', async () => {
    let sent: unknown = null;
    server.use(
      http.post('/api/pins/validate', async ({ request }) => {
        sent = await request.json();
        return HttpResponse.json({ valid: true, pins: WT32_PINS, issues: [] });
      }),
    );
    await validatePins({ preset: 'wt32-carrier-v1' });
    expect(sent).toEqual({ preset: 'wt32-carrier-v1' });
  });

  it('starts the wiring check and reads its results; a 409 carries the reason', async () => {
    expect((await fetchWiringCheck())?.state).toBe('idle');
    expect(await startWiringCheck()).toEqual({ ok: true, text: '' });
    expect(await fetchWiringCheck()).toEqual(mockWiringDone);
    server.use(
      http.post('/api/pins/check', () =>
        HttpResponse.json({ error: "Restart first: the saved pins aren't in effect yet" }, { status: 409 }),
      ),
    );
    expect(await startWiringCheck()).toEqual({ ok: false, text: "Restart first: the saved pins aren't in effect yet" });
    server.use(http.post('/api/pins/check', () => HttpResponse.error()));
    expect(await startWiringCheck()).toEqual({ ok: false, text: "The device didn't answer: try again." });
  });
});

describe('pinsView', () => {
  it('suggests the WT32 preset only when the boot probe found one', () => {
    expect(suggestedPreset(pinsDocumentFor('unset'))).toBe('wt32-carrier-v1');
    expect(suggestedPreset({ ...pinsDocumentFor('unset'), detected: null })).toBe('devkit');
    expect(suggestedPreset(null)).toBe('devkit');
  });

  it('a named preset takes its whole map; Custom keeps the pins', () => {
    const doc = pinsDocumentFor('devkit');
    expect(withPreset(doc, DEVKIT_PINS, 'wt32-carrier-v1')).toEqual(WT32_PINS);
    expect(withPreset(doc, WT32_PINS, 'custom')).toEqual({ ...WT32_PINS, preset: 'custom' });
  });

  it('the wiring check needs a board chosen, the saved pins in effect and valid', () => {
    const devkit = pinsDocumentFor('devkit');
    expect(checkBlocked(devkit)).toBeNull();
    expect(checkBlocked(null)).toMatch(/couldn't be read/);
    expect(checkBlocked(pinsDocumentFor('unset'))).toMatch(/Choose a board/);
    expect(checkBlocked({ ...devkit, saved: WT32_PINS })).toMatch(/Restart first/);
    expect(checkBlocked({ ...devkit, restartNeeded: true })).toMatch(/Restart first/);
    expect(checkBlocked({ ...devkit, valid: false })).toMatch(/Fix the pin setup/);
  });

  it('a board has Ethernet by the status Ethernet block or the board in effect', () => {
    expect(hasEthernet(mockStatus)).toBe(false);
    expect(hasEthernet(null)).toBe(false);
    expect(hasEthernet({ ...mockStatus, ethernet: { ...mockStatus.ethernet!, present: true } })).toBe(true);
    expect(hasEthernet({ ...mockStatus, ethernet: undefined, pins: { ...mockStatus.pins!, board: 'wt32-eth01' } })).toBe(true);
  });

  it('a restart is needed when the saved board is not the one in effect', () => {
    expect(pinsRestartNeeded(DEVKIT_PINS, mockStatus)).toBe(false);
    expect(pinsRestartNeeded(WT32_PINS, mockStatus)).toBe(true);
    expect(pinsRestartNeeded(DEVKIT_PINS, { ...mockStatus, pins: { ...mockStatus.pins!, restartNeeded: true } })).toBe(true);
    expect(pinsRestartNeeded(undefined, mockStatus)).toBe(false);
  });
});
