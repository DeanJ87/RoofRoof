import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import Wizard from '../Wizard';
import type { SystemStatus } from '../../types';
import { mockConfig, mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 018, continued from Wizard.test.tsx: the review rounds' cases. The wizard as a dialog of one-question screens; every
// spec 015 guarantee (saves through the settings API with the step, refused
// saves, discards on leaving a screen, Test freshness, skip, finish, no roof
// command on any screen) on the new screens.

const live = (status: SystemStatus = mockStatus) =>
  vi.mocked(useWebSocket<SystemStatus>).mockReturnValue({ data: status, connected: true, lastMessageAt: Date.now() });

// Every POST /api/config body, in order.
const recordSaves = (reply: () => Response = () => HttpResponse.json({ success: true })) => {
  const bodies: Record<string, unknown>[] = [];
  server.use(
    http.post('/api/config', async ({ request }) => {
      bodies.push((await request.json()) as Record<string, unknown>);
      return reply();
    }),
  );
  return bodies;
};

const at = (query: string) => window.history.replaceState(null, '', `/wizard?${query}`);
const onScreen = (id: string) =>
  waitFor(() => expect(document.querySelector('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id));
const button = (name: string) => screen.getByRole('button', { name });
const PASSWORD_SET = { auth: { enabled: true, username: 'admin', password: '********' } };

describe('Wizard: choices, skips and refusals', () => {
  beforeEach(() => live());
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('with a channel already saved, the chosen card follows the pick', async () => {
    mockDevice({ config: { setup: { state: 'done', step: 0 } } }); // Pushover saved on
    at('screen=alertChannel');
    render(<Wizard />);
    await onScreen('alertChannel');
    expect(screen.getByRole('radio', { name: 'Pushover' })).toBeChecked();
    fireEvent.click(screen.getByRole('radio', { name: 'ntfy' }));
    expect(screen.getByRole('radio', { name: 'ntfy' })).toBeChecked();
    fireEvent.click(screen.getByRole('radio', { name: 'Webhook' }));
    expect(screen.getByRole('radio', { name: 'Webhook' })).toBeChecked();
    // The pick survives the screen being rebuilt (Escape, then Keep going).
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    fireEvent.click(await screen.findByRole('button', { name: 'Keep going' }));
    await onScreen('alertChannel');
    expect(screen.getByRole('radio', { name: 'Webhook' })).toBeChecked();
  });

  it('Skip for now picks the next screen from the settings as saved', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 8 }, alerts: { ...mockConfig.alerts!, enabled: false } } });
    render(<Wizard />);
    await onScreen('alertChannel');
    fireEvent.click(screen.getByRole('radio', { name: 'ntfy' }));
    fireEvent.click(button('Skip for now'));
    await onScreen('daylight'); // alerts stay off, so their details are left out
  });

  it('a refused save moves focus to its reason, which is announced', async () => {
    mockDevice({ config: { ...PASSWORD_SET, setup: { state: 'new', step: 0 } } });
    recordSaves(() => HttpResponse.json({ error: 'The roof is moving: wait until it stops' }, { status: 409 }));
    render(<Wizard />);
    await onScreen('name');
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Backyard roof' } });
    fireEvent.click(button('Continue'));
    const alert = await screen.findByRole('alert');
    expect(alert).toHaveTextContent('The roof is moving: wait until it stops');
    await waitFor(() => expect(document.activeElement).toBe(alert));
  });

  it('without a location, the daylight screen says where it is set and goes there, keeping the answer', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 9 }, location: { ...mockConfig.location!, set: false } } });
    const saves = recordSaves();
    render(<Wizard />);
    await onScreen('daylight');
    fireEvent.click(screen.getByRole('radio', { name: 'Alert and close' }));
    expect(screen.getByText('Daylight needs your location, which is set on the Site screen earlier in setup.')).toBeInTheDocument();
    fireEvent.click(button('Set location'));
    await onScreen('site');
    expect(saves).toHaveLength(1);
    expect(saves[0].daylight).toMatchObject({ mode: 'close' });
    expect(saves[0].setup).toEqual({ state: 'new', step: 1 });
  });
});
