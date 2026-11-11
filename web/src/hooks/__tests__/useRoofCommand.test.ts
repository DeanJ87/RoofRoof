import { afterEach, describe, expect, it, vi } from 'vitest';
import { ROOF_COMMAND_HEADER } from '../../lib/api';
import { readAnswer, sendRoofCommand } from '../useRoofCommand';

// Spec 009 FR-006, FR-007, FR-009: each answer the device can give to a roof
// command, read into what the card shows; every command carries the header.

const json = (status: number, body: unknown) =>
  new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } });
const at = () => 5000;

describe('readAnswer', () => {
  it('200 is accepted, with nothing to say', async () => {
    expect(await readAnswer('open', json(200, { success: true, state: 'opening' }), 5000, 'closed')).toMatchObject({
      outcome: 'accepted',
      text: '',
      at: 5000,
      stateAt: 'closed',
    });
  });

  it('202 is a waiting close, with the device’s message', async () => {
    const body = { success: true, state: 'open', pending: true, message: 'Close starts once this has stopped: MQTT is connecting' };
    expect(await readAnswer('close', json(202, body), 1, 'open')).toMatchObject({ outcome: 'pending', text: body.message });
  });

  it.each(['busy', 'restarting', 'hotspot', 'weatherNotSafe', 'mountNotParked'])('409 %s keeps the reason and the text', async (reason) => {
    const answer = await readAnswer(
      'open',
      json(409, { success: false, state: 'closed', reason, error: `Refused: ${reason}` }),
      1,
      'closed',
    );
    expect(answer).toMatchObject({ outcome: 'refused', reason, text: `Refused: ${reason}` });
  });

  it('a device text it knows is translated (here: the hotspot refusal)', async () => {
    const error = "Opening isn't available while the setup hotspot is on";
    expect((await readAnswer('open', json(409, { reason: 'hotspot', error }), 1, null)).text).toBe(error);
  });

  it('401 and 403 show the device’s error, or a fallback', async () => {
    expect(await readAnswer('stop', json(403, { error: 'Roof commands need the header X-RoofRoof: 1' }), 1, null)).toMatchObject({
      outcome: 'error',
      text: 'Roof commands need the header X-RoofRoof: 1',
    });
    expect((await readAnswer('open', new Response('', { status: 401 }), 1, null)).text).toBe('The device refused the command');
  });
});

describe('sendRoofCommand', () => {
  afterEach(() => vi.unstubAllGlobals());

  it.each(['open', 'close', 'stop', 'clear-fault'] as const)('%s is a POST with the command header and no body', async (command) => {
    const fetchMock = vi.fn().mockResolvedValue(json(200, { success: true }));
    vi.stubGlobal('fetch', fetchMock);
    await sendRoofCommand(command, 'closed', at);
    expect(fetchMock).toHaveBeenCalledWith(`/api/roof/${command}`, { method: 'POST', headers: { [ROOF_COMMAND_HEADER]: '1' } });
  });

  it('no answer: unreachable, and Stop says it isn’t confirmed', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new TypeError('Failed to fetch')));
    expect(await sendRoofCommand('open', 'closed', at)).toMatchObject({ outcome: 'unreachable', text: "Couldn't reach the device" });
    expect((await sendRoofCommand('stop', 'opening', at)).text).toBe("Stop not confirmed: couldn't reach the device");
  });
});
