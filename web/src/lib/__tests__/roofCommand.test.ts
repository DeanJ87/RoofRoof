import { afterEach, describe, expect, it, vi } from 'vitest';
import { ROOF_COMMAND_HEADER, roofCommand } from '../api';

// Roof commands carry the X-RoofRoof header the device requires (spec 003):
// a cross-site form can't send it.
describe('roofCommand', () => {
  afterEach(() => vi.unstubAllGlobals());

  it('posts to the roof route with the command header', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response('{"success":true,"state":"opening"}'));
    vi.stubGlobal('fetch', fetchMock);
    await roofCommand('open');
    expect(fetchMock).toHaveBeenCalledWith('/api/roof/open', expect.objectContaining({ method: 'POST', headers: { 'X-RoofRoof': '1' } }));
  });

  it('sends the header for every command', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response('{}'));
    vi.stubGlobal('fetch', fetchMock);
    for (const command of ['open', 'close', 'stop', 'clear-fault'] as const) await roofCommand(command);
    for (const [path, init] of fetchMock.mock.calls) {
      expect(path).toMatch(/^\/api\/roof\/(open|close|stop|clear-fault)$/);
      expect((init as RequestInit).headers).toEqual({ [ROOF_COMMAND_HEADER]: '1' });
    }
  });
});
