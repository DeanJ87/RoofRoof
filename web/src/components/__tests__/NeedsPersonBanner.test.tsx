import { beforeEach, describe, it, expect, vi } from 'vitest';
import { http, HttpResponse } from 'msw';
import { render, screen } from '@testing-library/preact';
import NeedsPersonBanner from '../NeedsPersonBanner';
import { mockStatus } from '../../mocks/data';
import { server } from '../../test/mswServer';
import type { SystemStatus } from '../../types';

// Spec 045: the live socket is mocked; null data (no frame yet) reads GET /api/status.
vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';
const liveStatus = (data: SystemStatus | null) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data, connected: data !== null, lastMessageAt: data ? Date.now() : null });

// Spec 030: while the roof needs a person, every page says so in red with the
// device's words; otherwise (and on older firmware) nothing shows.

const withNeedsPerson = (needsPerson?: { active: boolean; text: string }) =>
  server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, roof: { ...mockStatus.roof, needsPerson } })));

const settle = () => new Promise((resolve) => setTimeout(resolve, 50));

describe('NeedsPersonBanner', () => {
  beforeEach(() => {
    liveStatus(null);
  });

  it('takes the state from the live status without reading GET /api/status (spec 045)', async () => {
    const text = 'Hall sensor failed.';
    let reads = 0;
    server.use(
      http.get('/api/status', () => {
        reads += 1;
        return HttpResponse.json(mockStatus);
      }),
    );
    liveStatus({ ...mockStatus, roof: { ...mockStatus.roof, needsPerson: { active: true, text } } } as SystemStatus);
    render(<NeedsPersonBanner />);
    const banner = await screen.findByRole('status');
    expect(banner.querySelector('.banner-detail')).toHaveTextContent(text);
    await settle();
    expect(reads).toBe(0);
  });

  it('shows the device text in red while the roof needs a person', async () => {
    const text = 'Position unknown: the Hall sensor reads between (2050 counts), not a magnet.';
    withNeedsPerson({ active: true, text });
    render(<NeedsPersonBanner />);
    const banner = await screen.findByRole('status');
    expect(banner).toHaveClass('banner', 'banner-bad', 'needs-person-banner');
    expect(banner.querySelector('.banner-title')).toHaveTextContent('The roof needs a person');
    expect(banner.querySelector('.banner-detail')).toHaveTextContent(text);
  });

  it.each([{ active: false, text: '' }, undefined])('shows nothing for %o', async (needsPerson) => {
    withNeedsPerson(needsPerson);
    const { container } = render(<NeedsPersonBanner />);
    await settle();
    expect(container.querySelector('.needs-person-banner')).toBeNull();
  });

  it('shows nothing while the status can not be read', async () => {
    server.use(http.get('/api/status', () => HttpResponse.error()));
    const { container } = render(<NeedsPersonBanner />);
    await settle();
    expect(container.querySelector('.needs-person-banner')).toBeNull();
  });
});
