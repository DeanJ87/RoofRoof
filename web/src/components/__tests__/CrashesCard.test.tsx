import { afterEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import CrashesCard from '../diagnostics/CrashesCard';
import Diagnostics from '../Diagnostics';
import { server } from '../../test/mswServer';
import { mockCrashesText, mockDeviceLogText } from '../../mocks/crash';
import { hasRecords } from '../../lib/crashApi';

// Spec 029 FR-019: the Crashes card on the Diagnostics page.

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn(() => ({ data: null, connected: false, lastMessageAt: null })) }));

const NONE = 'This start: poweron (1), boot 1\nRunning: 1.0, build ?\nRecords: 0 (the last 3 are kept)\n';

const clipboard = () => {
  const writeText = vi.fn((_text: string) => Promise.resolve());
  Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
  return writeText;
};

afterEach(() => {
  vi.restoreAllMocks();
});

describe('Crashes card', () => {
  it('tells a document with records from one without', () => {
    expect(hasRecords(mockCrashesText)).toBe(true);
    expect(hasRecords(NONE)).toBe(false);
  });

  it('shows the records as the device wrote them and copies them', async () => {
    const writeText = clipboard();
    render(<CrashesCard />);
    expect(await screen.findByText(/Reason: LoadProhibited/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Copy' }));
    await waitFor(() => expect(writeText).toHaveBeenCalledWith(mockCrashesText));
  });

  it('copies the log', async () => {
    const writeText = clipboard();
    render(<CrashesCard />);
    await screen.findByText(/Reason: LoadProhibited/);
    fireEvent.click(screen.getByRole('button', { name: 'Copy log' }));
    await waitFor(() => expect(writeText).toHaveBeenCalledWith(mockDeviceLogText));
  });

  it('says when nothing was recorded, with no Copy', async () => {
    server.use(http.get('/api/diag/crashes', () => HttpResponse.text(NONE)));
    render(<CrashesCard />);
    expect(await screen.findByText('No crashes recorded.')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Copy' })).toBeNull();
    expect(screen.getByRole('button', { name: 'Copy log' })).toBeInTheDocument();
  });

  it('says when the records can not be read', async () => {
    server.use(http.get('/api/diag/crashes', () => new HttpResponse(null, { status: 401 })));
    render(<CrashesCard />);
    expect(await screen.findByText(/Crash records can't be read/)).toBeInTheDocument();
  });

  it('shows on the Diagnostics page with diagnostics off', async () => {
    render(<Diagnostics />);
    const card = (await screen.findByRole('heading', { name: 'Crashes' })).closest('section') as HTMLElement;
    expect(await within(card).findByText(/Reason: LoadProhibited/)).toBeInTheDocument();
  });
});
