import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import Wizard from '../Wizard';
import type { SystemStatus } from '../../types';
import { mockConfig, mockStatus } from '../../mocks/data';
import { mockDevice } from '../../test/mockDevice';
import { server } from '../../test/mswServer';

vi.mock('../../hooks/useWebSocket', () => ({ useWebSocket: vi.fn() }));
import { useWebSocket } from '../../hooks/useWebSocket';

// Spec 025 (FR-021 to FR-023): the alerts screen. Also see Wizard.test.tsx.
// Specs 015, 018 and 025: the wizard as a dialog of one-question screens in the
// installer's order; every spec 015 guarantee (saves through the settings API
// with the part, refused saves, discards on leaving a screen, Test freshness,
// skip, finish); progress that survives a reload in every setup state; alerts
// as a multiple choice with their fields inline.

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

const onScreen = (id: string) =>
  waitFor(() => expect(document.querySelector('[data-wizard-screen]')).toHaveAttribute('data-wizard-screen', id));
const button = (name: string) => screen.getByRole('button', { name });

describe('Wizard alerts', () => {
  beforeEach(() => live());
  afterEach(() => window.history.replaceState(null, '', '/'));

  describe('alerts (FR-021 to FR-023)', () => {
    const noChannels = {
      ...mockConfig.alerts!,
      enabled: true,
      pushover: { enabled: false, userKey: '', appToken: '', sound: '' },
      ntfy: { ...mockConfig.alerts!.ntfy, enabled: false },
    };

    it('ticks several channels, each with its fields, and unticks', async () => {
      mockDevice({ config: { alerts: noChannels, setup: { state: 'new', step: 9 } } });
      const saves = recordSaves();
      render(<Wizard />);
      await onScreen('alerts');
      const group = screen.getByRole('group', { name: 'Where should alerts reach you?' });
      expect(button('Continue')).toBeDisabled();
      fireEvent.click(within(group).getByRole('checkbox', { name: /ntfy/ }));
      fireEvent.click(within(group).getByRole('checkbox', { name: /Webhook/ }));
      expect(within(group).getByRole('checkbox', { name: /ntfy/ })).toBeChecked();
      expect(within(group).getByRole('checkbox', { name: /Webhook/ })).toBeChecked();
      expect(screen.getByLabelText('Topic', { selector: 'input' })).toBeInTheDocument();
      expect(screen.getByLabelText('URL', { selector: 'input' })).toBeInTheDocument();
      fireEvent.click(within(group).getByRole('checkbox', { name: /Webhook/ })); // untick
      expect(screen.queryByLabelText('URL', { selector: 'input' })).toBeNull();
      fireEvent.input(screen.getByLabelText('Topic', { selector: 'input' }), { target: { value: 'my-roof' } });
      fireEvent.click(button('Continue'));
      await onScreen('password');
      expect(saves[0].alerts).toEqual({ ntfy: { enabled: true, topic: 'my-roof' } });
    });

    it('"No alerts for now" clears every channel and says so', async () => {
      mockDevice({ config: { setup: { state: 'new', step: 9 } } }); // Pushover on
      const saves = recordSaves();
      render(<Wizard />);
      await onScreen('alerts');
      const group = screen.getByRole('group', { name: 'Where should alerts reach you?' });
      expect(within(group).getByRole('checkbox', { name: /Pushover/ })).toBeChecked();
      fireEvent.click(within(group).getByRole('checkbox', { name: /No alerts for now/ }));
      expect(within(group).getByRole('checkbox', { name: /Pushover/ })).not.toBeChecked();
      expect(within(group).getByRole('checkbox', { name: /No alerts for now/ })).toBeChecked();
      expect(screen.getByText('No alert channel: critical alerts will reach no one.')).toBeInTheDocument();
      fireEvent.click(button('Continue'));
      await onScreen('password');
      expect(saves[0].alerts).toEqual({ pushover: { enabled: false } });
    });

    it('Pushover shows its keys here; a refused Continue puts each error under its field and says so', async () => {
      mockDevice({ config: { alerts: noChannels, setup: { state: 'new', step: 9 } } });
      const saves = recordSaves();
      render(<Wizard />);
      await onScreen('alerts');
      fireEvent.click(
        within(screen.getByRole('group', { name: 'Where should alerts reach you?' })).getByRole('checkbox', { name: /Pushover/ }),
      );
      const key = screen.getByLabelText('User key', { selector: 'input' });
      const token = screen.getByLabelText('App token', { selector: 'input' });
      fireEvent.click(button('Continue'));
      expect(await screen.findByText('Fix the fields marked below.')).toBeInTheDocument();
      expect(saves).toHaveLength(0);
      expect(key.closest('.field')).toHaveTextContent('The user key is the 30-character key on your Pushover dashboard');
      expect(token.closest('.field')).toHaveTextContent('The app token is the 30-character API token of your Pushover application');
      await onScreen('alerts');
    });

    // FR-007: MQTT's broker on the same screen, never "Turn on MQTT" in Settings.
    it('MQTT ticked with MQTT off turns it on and shows the broker here', async () => {
      mockDevice({
        config: { alerts: noChannels, mqtt: { ...mockConfig.mqtt, enabled: false, broker: '' }, setup: { state: 'new', step: 9 } },
      });
      const saves = recordSaves();
      render(<Wizard />);
      await onScreen('alerts');
      fireEvent.click(
        within(screen.getByRole('group', { name: 'Where should alerts reach you?' })).getByRole('checkbox', { name: /MQTT/ }),
      );
      const broker = screen.getByLabelText('Broker', { selector: 'input' });
      fireEvent.input(broker, { target: { value: '192.168.1.9' } });
      expect(screen.queryByRole('button', { name: 'Turn on MQTT' })).toBeNull();
      fireEvent.click(button('Continue'));
      await onScreen('password');
      expect(saves[0]).toMatchObject({ mqtt: { enabled: true, broker: '192.168.1.9' }, alerts: { mqtt: { enabled: true } } });
    });
  });
});
