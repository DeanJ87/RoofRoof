import { describe, it, expect } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Settings from '../Settings';
import { server } from '../../test/mswServer';

describe('Alert levels', () => {
  it('sets a level and Pushover sound per event and saves them', async () => {
    let saved: any = null;
    server.use(
      http.post('/api/config', async ({ request }) => {
        saved = await request.json();
        return HttpResponse.json({ success: true });
      }),
    );
    window.history.replaceState(null, '', '/settings?tab=alerts');
    render(<Settings />);

    const lostLevel = (await screen.findByLabelText('The imaging app stops checking: level')) as HTMLSelectElement;
    expect(lostLevel.value).toBe('3');
    fireEvent.change(lostLevel, { target: { value: '1' } });
    // Quiet alerts make no sound, so there's no sound to pick.
    await waitFor(() => expect(screen.queryByLabelText('The imaging app stops checking: sound')).toBeNull());

    fireEvent.change(screen.getByLabelText('The imaging app is back: level'), { target: { value: '4' } });
    fireEvent.change(await screen.findByLabelText('The imaging app is back: sound'), { target: { value: 'siren' } });
    fireEvent.click(screen.getByRole('button', { name: /save/i }));

    await waitFor(() => expect(saved).not.toBeNull());
    expect(saved.alerts.events.client_lost).toMatchObject({ level: 1 });
    expect(saved.alerts.events.client_back).toEqual({ level: 4, sound: 'siren' });
  });

  it('tests one event with its unsaved level and sound, and reports every channel', async () => {
    let query = '';
    let calls = 0;
    server.use(
      http.post('/api/alerts/test', ({ request }) => {
        query = new URL(request.url).search;
        return HttpResponse.json({ success: true }, { status: 202 });
      }),
      http.get('/api/alerts/recent', () => {
        calls += 1;
        const alerts =
          calls === 1
            ? []
            : [
                {
                  id: 9,
                  event: 'client_lost',
                  title: 'Test: Imaging app stopped checking',
                  message: '',
                  level: 'wake',
                  ageSeconds: 1,
                  channels: { pushover: { status: 'sent', detail: 'HTTP 200' } },
                },
              ];
        return HttpResponse.json({ enabled: true, alerts });
      }),
    );
    window.history.replaceState(null, '', '/settings?tab=alerts');
    render(<Settings />);

    fireEvent.change(await screen.findByLabelText('The imaging app stops checking: sound'), { target: { value: 'siren' } });
    const row = document.querySelector('[data-event="client_lost"]')!;
    fireEvent.click(within(row as HTMLElement).getByRole('button', { name: 'Test' }));

    expect(await screen.findByText('Pushover: Sent', {}, { timeout: 4000 })).toBeInTheDocument();
    expect(query).toBe('?channel=all&event=client_lost&level=3&sound=siren&title=&message=');
  });
});

describe('Alert wording', () => {
  it('writes a custom message with variables and sends it with the test', async () => {
    let query = '';
    let saved: any = null;
    server.use(
      http.post('/api/alerts/test', ({ request }) => {
        query = new URL(request.url).search;
        return HttpResponse.json({ success: true }, { status: 202 });
      }),
      http.post('/api/config', async ({ request }) => {
        saved = await request.json();
        return HttpResponse.json({ success: true });
      }),
    );
    window.history.replaceState(null, '', '/settings?tab=alerts');
    render(<Settings />);

    const row = (await screen.findByLabelText('The imaging app stops checking: level')).closest('.event-row')!;
    fireEvent.click(Array.from(row.querySelectorAll('button')).find((b) => b.textContent === 'Text')!);

    const message = screen.getByLabelText('Alert message') as HTMLTextAreaElement;
    expect(message.placeholder).toContain('{silent_for}');
    fireEvent.input(message, { target: { value: 'Unsafe x' } });
    fireEvent.focus(message);
    message.setSelectionRange(8, 8);
    fireEvent.click(screen.getByRole('button', { name: '{last_checked}' }));
    await waitFor(() => expect((screen.getByLabelText('Alert message') as HTMLTextAreaElement).value).toBe('Unsafe x{last_checked}'));

    fireEvent.click(Array.from(row.querySelectorAll('button')).find((b) => b.textContent === 'Test')!);
    await waitFor(() => expect(query).toContain('message=Unsafe%20x%7Blast_checked%7D'));

    fireEvent.click(screen.getByRole('button', { name: /save/i }));
    await waitFor(() => expect(saved).not.toBeNull());
    expect(saved.alerts.events.client_lost.message).toBe('Unsafe x{last_checked}');
  });
});
