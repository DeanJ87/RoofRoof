import { describe, expect, it } from 'vitest';
import { renderHook, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import { useWiringCheck } from '../../hooks/usePins';
import { server } from '../../test/mswServer';

// Spec 023: the wiring check polls until it is done, and stops for good when
// its page goes away, even with a poll still in flight then.

describe('useWiringCheck', () => {
  it('a poll answered after unmount arms no further poll', async () => {
    let asked = 0;
    let answer: () => void = () => undefined;
    server.use(
      http.get('/api/pins/check', async () => {
        asked += 1;
        // The first answer is held until the hook has unmounted.
        if (asked === 1) await new Promise<void>((resolve) => (answer = resolve));
        return HttpResponse.json({ state: 'running', checks: [] });
      }),
    );
    const { unmount } = renderHook(() => useWiringCheck(5));
    await waitFor(() => expect(asked).toBe(1));
    unmount();
    answer();
    // Long enough for several polls at 5 ms had one been armed.
    await new Promise((resolve) => setTimeout(resolve, 60));
    expect(asked).toBe(1);
  });

  it('keeps polling while mounted until the check is done', async () => {
    let asked = 0;
    server.use(
      http.get('/api/pins/check', () => {
        asked += 1;
        return HttpResponse.json(asked < 3 ? { state: 'running', checks: [] } : { state: 'done', checks: [] });
      }),
    );
    const { result, unmount } = renderHook(() => useWiringCheck(5));
    await waitFor(() => expect(result.current.check?.state).toBe('done'));
    expect(asked).toBe(3);
    unmount();
  });
});
