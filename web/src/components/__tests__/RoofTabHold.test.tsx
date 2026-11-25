import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import type { Config } from '../../types';

// Spec 030's settings on Settings → Roof: the automatic close's unsafe hold
// and N.I.N.A.-lost close (Automatic close card), the blind close (Motion card).

const openRoofTab = async () => {
  window.history.replaceState(null, '', '/settings?tab=roof');
  render(<Toaster />);
  render(<Settings />);
  expect(await screen.findByRole('tab', { name: 'Roof', selected: true })).toBeInTheDocument();
};
const field = (path: string) => document.querySelector<HTMLInputElement>(`[data-field="${path}"]`);
const findField = (path: string) =>
  waitFor(() => {
    const el = field(path);
    if (!el) throw new Error(`no ${path} yet`);
    return el;
  });

describe('Roof tab: weather hold, N.I.N.A. lost, blind close (spec 030)', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('saves the unsafe hold, the N.I.N.A.-lost close with its time, and the blind close settings (spec 030)', async () => {
    mockDevice();
    let sent: Config | null = null;
    server.use(
      http.post('/api/config', async ({ request }) => {
        sent = (await request.json()) as Config;
        return HttpResponse.json({ success: true });
      }),
    );
    await openRoofTab();
    fireEvent.change(await findField('autoClose.unsafeHoldSeconds'), { target: { value: '90' } });
    // The time shows once the close is on (off by default).
    expect(field('autoClose.ninaLostMinutes')).toBeNull();
    fireEvent.click(screen.getByLabelText('Close when N.I.N.A. is lost'));
    fireEvent.change(await findField('autoClose.ninaLostMinutes'), { target: { value: '10' } });
    fireEvent.change(field('roof.blindSagVolts')!, { target: { value: '2.5' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(sent).not.toBeNull());
    expect(sent!.autoClose).toEqual({ unsafeHoldSeconds: 90, ninaLostClose: true, ninaLostMinutes: 10 });
    expect(sent!.roof).toEqual({ blindSagVolts: 2.5 });
  });

  it('refuses an unsafe hold above 300 s, and hides the stall sag while the blind close is off', async () => {
    mockDevice();
    await openRoofTab();
    fireEvent.change(await findField('autoClose.unsafeHoldSeconds'), { target: { value: '301' } });
    expect(await screen.findByText('Unsafe hold must be between 0 and 300 seconds')).toBeInTheDocument();
    fireEvent.click(screen.getByLabelText('Blind close'));
    await waitFor(() => expect(field('roof.blindSagVolts')).toBeNull());
  });
});
