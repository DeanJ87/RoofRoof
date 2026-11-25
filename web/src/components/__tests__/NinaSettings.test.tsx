import { afterEach, describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';

// Spec 027 (FR-013, FR-022, US4): Settings → Roof → Mount park: the N.I.N.A. address, its Test with
// the answer, then "May ask N.I.N.A. to park", in that order.

const openRoofTab = async () => {
  window.history.replaceState(null, '', '/settings?tab=roof');
  render(<Settings />);
  return waitFor(() => {
    const url = document.querySelector<HTMLInputElement>('[data-field="park.nina.url"]');
    if (!url) throw new Error('no address yet');
    return url;
  });
};
const testButton = () => document.querySelector('[data-field="park.nina.test"] button') as HTMLButtonElement;

describe('the N.I.N.A. card', () => {
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('orders the address, Test and the park requests, and takes https', async () => {
    mockDevice();
    const url = await openRoofTab();
    expect(url.placeholder).toBe('http://192.168.1.20:1888 or https://…');
    const toggle = document.querySelector('[data-field="park.requestPark"]')!;
    const follows = (a: Node, b: Node) => Boolean(a.compareDocumentPosition(b) & Node.DOCUMENT_POSITION_FOLLOWING);
    expect(follows(url, testButton())).toBe(true);
    expect(follows(testButton(), toggle)).toBe(true);
    fireEvent.input(url, { target: { value: 'https://nina.local:1888' } });
    await waitFor(() => expect(url.value).toBe('https://nina.local:1888'));
    expect(url.getAttribute('aria-invalid')).not.toBe('true');
    expect(testButton()).toBeEnabled();
  });

  it('Test sends the typed address and shows the answer as rows', async () => {
    mockDevice();
    let body: unknown = null;
    server.use(
      http.post('/api/nina/test', async ({ request }) => {
        body = await request.json();
        return HttpResponse.json({ reachable: true, connected: true, atPark: false, slewing: false });
      }),
    );
    await openRoofTab();
    fireEvent.click(testButton());
    const answer = (await screen.findByText('N.I.N.A. answered')).closest('[role="status"]') as HTMLElement;
    expect(body).toEqual({ url: 'http://192.168.1.20:1888' });
    expect(answer).toHaveTextContent('N.I.N.A. answeredYes');
    expect(answer).toHaveTextContent('Mount connectedYes');
    expect(answer).toHaveTextContent('Mount parkedNo');
  });

  it('says why not when N.I.N.A. does not answer', async () => {
    mockDevice();
    server.use(
      http.post('/api/nina/test', () =>
        HttpResponse.json(
          { reachable: false, connected: false, atPark: false, slewing: false, error: 'N.I.N.A. not responding: connection refused' },
          { status: 502 },
        ),
      ),
    );
    await openRoofTab();
    fireEvent.click(testButton());
    expect(await screen.findByText('N.I.N.A. not responding: connection refused')).toBeInTheDocument();
    expect((screen.getByText('N.I.N.A. answered').closest('[role="status"]') as HTMLElement).textContent).toContain('No');
    expect(screen.queryByText('Mount connected')).toBeNull();
  });
});
