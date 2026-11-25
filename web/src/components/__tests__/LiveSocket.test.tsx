import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { act, renderHook } from '@testing-library/preact';
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 028 FR-001 to FR-004: a component only gets readings from the current
// connection; a drop or a quiet socket takes them away until the next one.

class FakeSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;
  static made: FakeSocket[] = [];
  readyState = FakeSocket.CONNECTING;
  onopen: (() => void) | null = null;
  onmessage: ((event: { data: string }) => void) | null = null;
  onclose: (() => void) | null = null;
  closed = false;
  constructor(readonly url: string) {
    FakeSocket.made.push(this);
  }
  open() {
    this.readyState = FakeSocket.OPEN;
    this.onopen?.();
  }
  send(data: unknown) {
    this.onmessage?.({ data: JSON.stringify(data) });
  }
  drop() {
    this.readyState = FakeSocket.CLOSED;
    this.onclose?.();
  }
  close() {
    this.closed = true;
    this.readyState = FakeSocket.CLOSED;
  }
}

const last = () => FakeSocket.made[FakeSocket.made.length - 1];

describe('useWebSocket', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    FakeSocket.made = [];
    vi.stubGlobal('WebSocket', FakeSocket);
  });
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  it('has no reading until the first message, then the reading', () => {
    const { result, unmount } = renderHook(() => useWebSocket<{ n: number }>('/ws/a'));
    expect(result.current.data).toBeNull();
    expect(result.current.reconnecting).toBe(false);
    act(() => last().open());
    expect(result.current.connected).toBe(true);
    expect(result.current.data).toBeNull();
    act(() => last().send({ n: 1 }));
    expect(result.current.data).toEqual({ n: 1 });
    unmount();
  });

  it('drops the reading when the connection closes, and says it is reconnecting', () => {
    const { result, unmount } = renderHook(() => useWebSocket<{ n: number }>('/ws/b'));
    act(() => {
      last().open();
      last().send({ n: 1 });
    });
    act(() => last().drop());
    expect(result.current.data).toBeNull();
    expect(result.current.connected).toBe(false);
    expect(result.current.reconnecting).toBe(true);
    // The first retry after about a second, not five.
    act(() => {
      vi.advanceTimersByTime(2000);
    });
    expect(FakeSocket.made).toHaveLength(2);
    act(() => {
      last().open();
      last().send({ n: 2 });
    });
    expect(result.current.data).toEqual({ n: 2 });
    expect(result.current.reconnecting).toBe(false);
    unmount();
  });

  it('closes a socket that has been quiet for 5 s and opens a new one at once', () => {
    const { result, unmount } = renderHook(() => useWebSocket<{ n: number }>('/ws/c'));
    act(() => {
      last().open();
      last().send({ n: 1 });
    });
    const first = last();
    act(() => {
      vi.advanceTimersByTime(4000);
    });
    expect(result.current.data).toEqual({ n: 1 });
    act(() => {
      vi.advanceTimersByTime(2000);
    });
    expect(first.closed).toBe(true);
    expect(FakeSocket.made).toHaveLength(2);
    expect(result.current.data).toBeNull();
    expect(result.current.reconnecting).toBe(true);
    unmount();
  });

  it('reconnects at once when the page becomes visible during the back-off', () => {
    const { unmount } = renderHook(() => useWebSocket<{ n: number }>('/ws/d'));
    act(() => last().open());
    act(() => last().drop());
    expect(FakeSocket.made).toHaveLength(1);
    act(() => {
      window.dispatchEvent(new Event('online'));
    });
    expect(FakeSocket.made).toHaveLength(2);
    unmount();
  });

  it('closes the socket when the last component leaves', () => {
    const { unmount } = renderHook(() => useWebSocket('/ws/e'));
    const socket = last();
    unmount();
    expect(socket.closed).toBe(true);
  });
});
