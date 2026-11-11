import { describe, it, expect } from 'vitest';
import { http, HttpResponse } from 'msw';
import { render, screen } from '@testing-library/preact';
import BenchBanner from '../BenchBanner';
import { mockStatus } from '../../mocks/data';
import { server } from '../../test/mswServer';

// Spec 011 FR-026: bench firmware says so on every page; a real roof (and the
// demo) shows nothing.

const withSimulation = (simulation: 'off' | 'bench' | 'demo') =>
  server.use(http.get('/api/status', () => HttpResponse.json({ ...mockStatus, roof: { ...mockStatus.roof, simulation } })));

describe('BenchBanner', () => {
  it('warns on a bench board', async () => {
    withSimulation('bench');
    render(<BenchBanner />);
    const banner = await screen.findByRole('status');
    expect(banner).toHaveTextContent(/Bench mode: this roof is simulated and the motor is never driven/);
  });

  it.each(['off', 'demo'] as const)('shows nothing for %s', async (simulation) => {
    withSimulation(simulation);
    const { container } = render(<BenchBanner />);
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(container.querySelector('.bench-banner')).toBeNull();
  });

  it('shows nothing while the status can not be read', async () => {
    server.use(http.get('/api/status', () => HttpResponse.error()));
    const { container } = render(<BenchBanner />);
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(container.querySelector('.bench-banner')).toBeNull();
  });
});
