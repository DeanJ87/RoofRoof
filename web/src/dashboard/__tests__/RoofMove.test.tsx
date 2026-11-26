import { describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import { server } from '../../test/mswServer';
import { mockConfig, mockStatus } from '../../mocks/data';
import type { RoofStatus, SystemStatus } from '../../types';
import type { LiveState } from '../../lib/roofView';
import RoofCard from '../RoofCard';

// Spec 027 on the Roof card: Open and Close stay usable while a check blocks them, the move dialog
// (FR-011) with its overrides (FR-007, FR-009), the park wait, the hold, the Hall rail fault and
// the sources' health.

const status = (roof: Partial<RoofStatus> = {}): SystemStatus => {
  const s = structuredClone(mockStatus);
  s.roof = { ...s.roof!, ...roof };
  return s;
};
const live = (s: SystemStatus): LiveState => ({ status: s, connected: true, quiet: false, reconnectedAt: null });
const show = (roof: Partial<RoofStatus>, confirm = false) =>
  render(<RoofCard live={live(status(roof))} config={mockConfig.roof} confirm={confirm} />);
const button = (name: string) => screen.getByRole('button', { name });
const dialog = () => screen.getByRole('dialog');

// Every roof command the card sends, with its body.
const record = () => {
  const sent: { command: string; body: string }[] = [];
  server.use(
    http.post('/api/roof/:command', async ({ params, request }) => {
      sent.push({ command: String(params.command), body: await request.text() });
      return HttpResponse.json({ success: true, pending: true, message: 'Waiting for the park' }, { status: 202 });
    }),
  );
  return sent;
};

const daylight = { mode: 'close', state: 'day', sunAltitudeDeg: 3.2, limitDeg: 0, text: 'Sun 3.2° ≥ 0.0°', closes: true } as const;

describe('the move dialog', () => {
  it('lists every reason Open is blocked and opens anyway, naming the overrides', async () => {
    const sent = record();
    show({ weather: 'unsafe', weatherText: 'unsafe: cloud', daylight, openOverridable: ['weatherNotSafe', 'daylight'] });
    fireEvent.click(button('Open'));
    expect(within(dialog()).getByRole('heading', { name: '2 reasons Open is blocked' })).toBeInTheDocument();
    expect(dialog()).toHaveTextContent('Weather check: unsafe: cloud');
    expect(dialog()).toHaveTextContent('Daylight check: the sun is up');
    expect(sent).toEqual([]);
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Open anyway' }));
    await waitFor(() => expect(sent).toEqual([{ command: 'open', body: '{"override":["weatherNotSafe","daylight"]}' }]));
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('spec 039: an Open past the park warns that the roof passes over the telescope, and names the park', async () => {
    const sent = record();
    show({ daylight, parkConfirmed: false, openOverridable: ['mountNotParked', 'daylight'] });
    fireEvent.click(button('Open'));
    expect(within(dialog()).getByRole('heading', { name: '2 reasons Open is blocked' })).toBeInTheDocument();
    expect(dialog()).toHaveTextContent('Mount not confirmed parked: the roof passes over the telescope. Check the camera first.');
    expect(dialog()).toHaveTextContent('Open anyway sets these checks aside for this opening. Look at the telescope first.');
    // More than the park in the way: nothing to wait for, so nothing is sent until the person chooses.
    expect(within(dialog()).queryByRole('button', { name: 'Wait for the park' })).toBeNull();
    expect(sent).toEqual([]);
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Open anyway' }));
    await waitFor(() => expect(sent).toEqual([{ command: 'open', body: '{"override":["mountNotParked","daylight"]}' }]));
  });

  it('no Open anyway when the device does not offer the park', () => {
    show({ daylight, parkConfirmed: false, openOverridable: ['daylight'] });
    fireEvent.click(button('Open'));
    expect(within(dialog()).queryByRole('button', { name: 'Open anyway' })).toBeNull();
    expect(dialog()).toHaveTextContent("These checks can't be set aside");
  });

  it('offers no override for rain, only Cancel; the Stop bar is inside the dialog', () => {
    const sent = record();
    show({ weather: 'unsafe', weatherText: 'unsafe: rain', openOverridable: [] });
    fireEvent.click(button('Open'));
    expect(within(dialog()).getByRole('heading', { name: '1 reason Open is blocked' })).toBeInTheDocument();
    expect(within(dialog()).queryByRole('button', { name: 'Open anyway' })).toBeNull();
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Cancel' }));
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(sent).toEqual([]);
  });

  it('an Open only the park blocks goes at once (the device waits for the park), warns, and can open anyway', async () => {
    const sent = record();
    show({ parkConfirmed: false, openOverridable: ['mountNotParked'] });
    fireEvent.click(button('Open'));
    await waitFor(() => expect(sent).toEqual([{ command: 'open', body: '' }]));
    expect(dialog()).toHaveTextContent('Mount not confirmed parked: the roof passes over the telescope.');
    expect(within(dialog()).getByRole('button', { name: 'Wait for the park' })).toBeInTheDocument();
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Open anyway' }));
    await waitFor(() => expect(sent[1]).toEqual({ command: 'open', body: '{"override":["mountNotParked"]}' }));
  });

  it('Cancel after an Open that went stops its wait; Wait for the park leaves it waiting', async () => {
    const sent = record();
    const { unmount } = show({ parkConfirmed: false, openOverridable: ['mountNotParked'] });
    fireEvent.click(button('Open'));
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Cancel' }));
    await waitFor(() => expect(sent.map((s) => s.command)).toEqual(['open', 'stop']));
    unmount();
    sent.length = 0;
    show({ parkConfirmed: false, openOverridable: ['mountNotParked'] });
    fireEvent.click(button('Open'));
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Wait for the park' }));
    await waitFor(() => expect(sent).toEqual([{ command: 'open', body: '' }]));
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('a Close with the park not confirmed goes at once, warns, and can close anyway', async () => {
    const sent = record();
    show({ state: 'open', parkConfirmed: false, parkText: 'N.I.N.A. reports not parked' });
    fireEvent.click(button('Close'));
    await waitFor(() => expect(sent).toEqual([{ command: 'close', body: '' }]));
    expect(dialog()).toHaveTextContent("The mount isn't confirmed parked: the roof could hit the telescope.");
    expect(within(dialog()).getByRole('button', { name: 'Wait for the park' })).toBeInTheDocument();
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Close anyway' }));
    await waitFor(() => expect(sent[1]).toEqual({ command: 'close', body: '{"override":["mountNotParked"]}' }));
  });

  it('Cancel after a Close that went stops its wait; with "Ask before" the Close waits for the choice', async () => {
    const sent = record();
    const { unmount } = show({ state: 'open', parkConfirmed: false });
    fireEvent.click(button('Close'));
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Cancel' }));
    await waitFor(() => expect(sent.map((s) => s.command)).toEqual(['close', 'stop']));
    unmount();
    sent.length = 0;
    show({ state: 'open', parkConfirmed: false }, true);
    fireEvent.click(button('Close'));
    expect(sent).toEqual([]);
    fireEvent.click(within(dialog()).getByRole('button', { name: 'Wait for the park' }));
    await waitFor(() => expect(sent).toEqual([{ command: 'close', body: '' }]));
  });
});

describe('the card (spec 027)', () => {
  it('shows a move waiting for the park, with Stop', () => {
    // inventory: roof-park-wait
    show({ parkConfirmed: false, parkWait: { command: 'open', origin: 'person', leftSeconds: 200, asked: true } });
    expect(screen.getByText('Open waits for the mount to park: N.I.N.A. was asked, 3:20 left')).toBeInTheDocument();
    expect(button('Stop')).toBeEnabled();
  });

  it("says why the last wait ended, and what a person's override sets aside", () => {
    // inventory: roof-override
    show({ state: 'open', parkWaitEnded: 'Mount not parked after 240 s: Close not done', override: ['daylight'] });
    expect(screen.getByText('Mount not parked after 240 s: Close not done')).toBeInTheDocument();
    expect(screen.getByText('Overridden until the roof closes or these clear: Daylight')).toBeInTheDocument();
  });

  it('a held automatic close says it is held, when it lapses, and offers Release hold', () => {
    show({ state: 'open', closePhase: 'heldByPerson', closeRearmSeconds: 300 });
    // Spec 034: the close phase pill and its "?" say who held it; one line for when it lapses.
    expect(within(document.querySelector('[data-inventory="roof-close-phase"]') as HTMLElement).getByText('Held')).toBeInTheDocument();
    expect(screen.queryByText(/^Automatic close held: a person pressed Stop/)).toBeNull();
    expect(screen.getByText('Lapses in 5:00 with the weather safe, or once the roof is closed.')).toBeInTheDocument();
    expect(button('Release hold')).toBeInTheDocument();
  });

  it('a Hall rail fault shows the live reading and how it clears, with Clear fault', () => {
    const fault = { kind: 'hallRail', cause: 'logic' as const, measured: 0, limit: 50, text: 'Hall reading 0 at a rail' };
    show({ state: 'error', fault, hall: { level: 'rail', counts: 4 } });
    const card = document.querySelector('[data-inventory="roof-fault-card"]') as HTMLElement;
    expect(within(card).getByText('Hall reading')).toBeInTheDocument();
    expect(within(card).getByText('4')).toBeInTheDocument();
    expect(card).toHaveTextContent('At rest it clears itself after a minute of good readings; else press Clear fault.');
    expect(within(card).getByRole('button', { name: 'Clear fault' })).toBeInTheDocument();
  });

  it('says when SQMeter last replied behind "?", and a silent SQMeter or unreachable N.I.N.A. as notes', () => {
    // inventory: roof-sources
    const { unmount } = show({});
    const tips = Array.from(document.querySelectorAll('[data-inventory="roof-checks"] [role="tooltip"]')).map((tip) => tip.textContent);
    expect(tips).toContain('safeSQMeter last replied 12 s ago');
    expect(tips).toContain('parked (N.I.N.A. AtPark)N.I.N.A. answered 2 s ago');
    unmount();
    show({
      sqmeter: { on: true, state: 'unreachable', lastReplySeconds: 75, text: 'no reply' },
      nina: { set: true, state: 'unreachable', reachable: false, connected: false, atPark: false, ageSeconds: 80, text: 'refused' },
    });
    // Spec 034: a short note under each row; the device's reason is behind the row's "?".
    expect(screen.getByText('No reply from SQMeter for 1:15').classList.contains('note-warn')).toBe(true);
    expect(screen.getByText('N.I.N.A. not reachable').classList.contains('note-warn')).toBe(true);
    const after = Array.from(document.querySelectorAll('[data-inventory="roof-checks"] [role="tooltip"]')).map((tip) => tip.textContent);
    expect(after.some((tip) => tip?.endsWith('no reply'))).toBe(true);
    expect(after.some((tip) => tip?.endsWith('refused'))).toBe(true);
  });
});
