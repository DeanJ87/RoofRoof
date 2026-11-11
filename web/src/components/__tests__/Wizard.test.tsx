import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Wizard from '../Wizard';
import type { SystemStatus } from '../../types';
import { mockConfig, mockInputs, mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 015: the wizard's pages, saves through the settings API, Test, skip,
// and no roof command on any page (FR-013, SC-003).

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

const heading = () => screen.findByRole('heading', { level: 2, name: /^Step \d+ of 10/ });
const pageButton = (name: string) => within(screen.getByRole('navigation', { name: 'Setup pages' })).getByRole('button', { name });

describe('Wizard', () => {
  beforeEach(() => live());
  afterEach(() => window.history.replaceState(null, '', '/'));

  it('opens at the page saved last while setup is new', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 3 } } });
    render(<Wizard />);
    expect(await heading()).toHaveTextContent('Step 4 of 10: Hall sensor');
    expect(pageButton('4. Hall sensor')).toHaveAttribute('aria-current', 'step');
  });

  it('opens at page 1 from the System page whatever the saved page', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 5 } } });
    window.history.replaceState(null, '', '/wizard?step=1');
    render(<Wizard />);
    expect(await heading()).toHaveTextContent('Step 1 of 10: Device');
  });

  it('starts with password protection on, and warns when it is turned off', async () => {
    mockDevice({ config: { auth: { enabled: false, username: 'admin', password: '' }, setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    const toggle = await screen.findByLabelText('Password-protect changes');
    await waitFor(() => expect(toggle).toBeChecked());
    fireEvent.click(toggle);
    expect(await screen.findByText(/No password: anyone on your network can change settings and move the roof/)).toBeInTheDocument();
  });

  it('offers no roof command on any page', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    await heading();
    const pages = [
      '1. Device',
      '2. Site and time',
      '3. Hardware',
      '4. Hall sensor',
      '5. Roof motion',
      '6. Weather',
      '7. N.I.N.A.',
      '8. Alerts',
      '9. Daylight',
      '10. Review',
    ];
    for (const page of pages) {
      fireEvent.click(pageButton(page));
      const title = page.slice(page.indexOf(' ') + 1);
      await waitFor(() => expect(document.querySelector('.wizard-heading')).toHaveTextContent(`: ${title}`));
      for (const name of [/^open$/i, /^close$/i, /clear fault/i, /^stop$/i])
        expect(screen.queryByRole('button', { name }), `${page}: ${name}`).toBeNull();
    }
  });

  it('saves the page with the next step and moves on', async () => {
    mockDevice({ config: { auth: { enabled: true, username: 'admin', password: '********' }, setup: { state: 'new', step: 0 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await heading();
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Backyard roof' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save and continue' }));
    expect(await screen.findByRole('heading', { level: 2, name: 'Step 2 of 10: Site and time' })).toBeInTheDocument();
    expect(saves).toHaveLength(1);
    expect(saves[0].deviceName).toBe('Backyard roof');
    expect(saves[0].setup).toEqual({ state: 'new', step: 1 });
    // The saved edit stays: the next page's save sends it, not the value from before.
    fireEvent.click(screen.getByRole('button', { name: 'Skip page' }));
    await waitFor(() => expect(document.querySelector('.wizard-heading')).toHaveTextContent('Step 3 of 10'));
    expect(screen.queryByRole('button', { name: 'Save', exact: true })).toBeNull(); // nothing unsaved
    fireEvent.change(screen.getByLabelText('Park switches fitted', { selector: 'select' }), { target: { value: 'one' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save and continue' }));
    await waitFor(() => expect(saves).toHaveLength(2));
    expect(saves[1].deviceName).toBe('Backyard roof');
  });

  it('keeps the page and says why when the device refuses the save (the roof moving)', async () => {
    mockDevice({ config: { auth: { enabled: true, username: 'admin', password: '********' }, setup: { state: 'new', step: 0 } } });
    recordSaves(() => HttpResponse.json({ error: 'The roof is moving: wait until it stops' }, { status: 409 }));
    live({ ...mockStatus, roof: { ...mockStatus.roof!, state: 'opening' } });
    render(<Wizard />);
    await heading();
    expect(screen.getByText('The roof is moving: saves are refused until it stops')).toBeInTheDocument();
    fireEvent.input(screen.getByLabelText('Name', { selector: 'input' }), { target: { value: 'Backyard roof' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save and continue' }));
    expect(await screen.findByText('The roof is moving: wait until it stops')).toBeInTheDocument();
    expect(await heading()).toHaveTextContent('Step 1 of 10');
  });

  it('an unchanged page only records the step', async () => {
    mockDevice({ config: { auth: { enabled: true, username: 'admin', password: '********' }, setup: { state: 'new', step: 0 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await heading();
    fireEvent.click(screen.getByRole('button', { name: 'Save and continue' }));
    await waitFor(() => expect(document.querySelector('.wizard-heading')).toHaveTextContent('Step 2 of 10'));
    expect(saves).toEqual([{ setup: { state: 'new', step: 1 } }]);
  });

  it('leaving a page drops its unsaved edits: a later save never carries them (FR-007)', async () => {
    mockDevice({
      config: {
        setup: { state: 'new', step: 4 },
        roof: { ...mockConfig.roof!, travelMm: 1850, speedMmPerSecond: 38, topDutyPercent: 100 },
      },
    });
    const saves = recordSaves();
    render(<Wizard />);
    await heading();
    fireEvent.click(screen.getByRole('button', { name: 'Use first-run values' }));
    fireEvent.click(screen.getByRole('button', { name: 'Skip page' }));
    await waitFor(() => expect(document.querySelector('.wizard-heading')).toHaveTextContent('Step 6 of 10'));
    fireEvent.click(screen.getByRole('switch', { name: 'SQMeter over MQTT' }));
    fireEvent.click(screen.getByRole('button', { name: 'Save and continue' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect((saves[0].roof as { topDutyPercent: number }).topDutyPercent).toBe(100);
  });

  it('offers password protection once per visit, never again after it is turned off', async () => {
    mockDevice({ config: { auth: { enabled: false, username: 'admin', password: '' }, setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    const toggle = await screen.findByLabelText('Password-protect changes');
    await waitFor(() => expect(toggle).toBeChecked());
    fireEvent.click(toggle);
    fireEvent.click(screen.getByRole('button', { name: 'Skip page' }));
    await waitFor(() => expect(document.querySelector('.wizard-heading')).toHaveTextContent('Step 2 of 10'));
    fireEvent.click(screen.getByRole('button', { name: 'Back' }));
    await waitFor(() => expect(document.querySelector('.wizard-heading')).toHaveTextContent('Step 1 of 10'));
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(screen.getByLabelText('Password-protect changes')).not.toBeChecked();
  });

  it('Skip page and Back never save', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await heading();
    fireEvent.click(screen.getByRole('button', { name: 'Skip page' }));
    expect(await heading()).toHaveTextContent('Step 2 of 10');
    fireEvent.click(screen.getByRole('button', { name: 'Back' }));
    expect(await heading()).toHaveTextContent('Step 1 of 10');
    expect(saves).toHaveLength(0);
  });

  it('Test on the weather page shows the verdict and each source', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 5 } } });
    render(<Wizard />);
    expect(await heading()).toHaveTextContent('Weather');
    fireEvent.click(screen.getByRole('button', { name: 'Test' }));
    const result = await screen.findByText('Weather verdict', {}, { timeout: 3000 });
    expect(result.closest('[data-test-result]')).toHaveTextContent('Safe');
    expect(result.closest('[data-test-result]')).toHaveTextContent('Answering');
  });

  it('Test on the N.I.N.A. page waits for the saved address, then shows the mount', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 6 } } });
    const saves = recordSaves();
    let generation = 1;
    server.use(http.get('/api/roof/inputs', () => HttpResponse.json({ ...mockInputs, nina: { ...mockInputs.nina, generation } })));
    render(<Wizard />);
    expect(await heading()).toHaveTextContent('N.I.N.A.');
    fireEvent.input(screen.getByLabelText('N.I.N.A. address', { selector: 'input' }), { target: { value: 'http://10.0.0.5:1888' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save and test' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect(screen.getByText("Waiting for the device's next reading.")).toBeInTheDocument();
    generation = 2; // the device has started again with the new address
    const row = await screen.findByText('Mount connected', {}, { timeout: 4000 });
    expect(row.closest('[data-test-result]')).toHaveTextContent('At park');
    expect(saves[0].setup).toBeUndefined(); // a Test doesn't move the wizard on
  });

  it('warns before skipping setup, and only a confirmed skip sets it done', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 2 } } });
    const saves = recordSaves();
    render(<Wizard />);
    await heading();
    fireEvent.click(screen.getByRole('button', { name: 'Skip setup' }));
    expect(screen.getByText(/the roof may not close itself in bad weather or tell anyone/)).toBeInTheDocument();
    expect(saves).toHaveLength(0);
    fireEvent.click(screen.getByRole('button', { name: 'Skip setup anyway' }));
    await waitFor(() => expect(saves).toEqual([{ setup: { state: 'done', step: 0 } }]));
  });

  it('the review lists every check and Finish sets setup done', async () => {
    mockDevice({ config: { setup: { state: 'offered', step: 9 } } });
    const saves = recordSaves();
    render(<Wizard />);
    expect(await heading()).toHaveTextContent('Review');
    const checks = document.querySelectorAll('[data-check]');
    expect(checks).toHaveLength(13);
    expect(screen.getByRole('link', { name: 'Commissioning guide' })).toHaveAttribute(
      'href',
      expect.stringContaining('docs/hardware/commissioning.md'),
    );
    expect(screen.getByRole('link', { name: 'Gate for unattended use' })).toHaveAttribute('href', expect.stringContaining('#stage-11'));
    fireEvent.click(screen.getByRole('button', { name: 'Finish' }));
    expect(await screen.findByText(/Setup finished/)).toBeInTheDocument();
    expect(saves.at(-1)?.setup).toEqual({ state: 'done', step: 0 });
  });

  it('a page change moves focus to its heading', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 0 } } });
    render(<Wizard />);
    await heading();
    fireEvent.click(screen.getByRole('button', { name: 'Skip page' }));
    await waitFor(() => expect(document.activeElement).toHaveTextContent('Step 2 of 10: Site and time'));
  });

  it('Hall sensor not fitted explains and links the bench test', async () => {
    mockDevice({ config: { setup: { state: 'new', step: 3 } } });
    render(<Wizard />);
    await heading();
    fireEvent.click(screen.getByLabelText('Not fitted yet'));
    expect(screen.getByText(/the automatic close can't work/)).toBeInTheDocument();
    expect(screen.getByRole('link', { name: 'Bench test without the sensor' })).toHaveAttribute(
      'href',
      expect.stringContaining('quick-bench-test.md'),
    );
  });

  it('first-run values set the top duty to 40% while the motion is the factory default', async () => {
    mockDevice({
      config: {
        setup: { state: 'new', step: 4 },
        roof: { ...mockConfig.roof!, travelMm: 1850, speedMmPerSecond: 38, topDutyPercent: 100 },
      },
    });
    const saves = recordSaves();
    render(<Wizard />);
    await heading();
    fireEvent.click(screen.getByRole('button', { name: 'Use first-run values' }));
    fireEvent.click(screen.getByRole('button', { name: 'Save and continue' }));
    await waitFor(() => expect(saves).toHaveLength(1));
    expect((saves[0].roof as { topDutyPercent: number }).topDutyPercent).toBe(40);
  });
});
