import { describe, it, expect } from 'vitest';
import { http, HttpResponse } from 'msw';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import Settings from '../Settings';
import { mockConfig } from '../../mocks/data';
import { server } from '../../test/mswServer';

describe('MQTT publish settings', () => {
  it('turns on Home Assistant discovery', async () => {
    let saved: any = null;
    server.use(
      http.get('/api/config', () =>
        HttpResponse.json({ ...mockConfig, mqtt: { ...mockConfig.mqtt, enabled: true, broker: '192.168.1.5', topic: 'roofroof' } }),
      ),
      http.post('/api/config', async ({ request }) => {
        saved = await request.json();
        return HttpResponse.json({ success: true });
      }),
    );
    window.history.replaceState(null, '', '/settings?tab=network');
    render(<Settings />);

    fireEvent.click(await screen.findByLabelText('MQTT discovery'));
    expect(await screen.findByPlaceholderText('homeassistant')).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: /save/i }));
    await waitFor(() => expect(saved).not.toBeNull());
    expect(saved.mqtt.homeAssistant).toEqual({ enabled: true, discoveryPrefix: 'homeassistant' });
  });

  it('rejects a base topic with MQTT wildcards', async () => {
    server.use(
      http.get('/api/config', () =>
        HttpResponse.json({ ...mockConfig, mqtt: { ...mockConfig.mqtt, enabled: true, broker: '192.168.1.5', topic: 'roofroof' } }),
      ),
    );
    window.history.replaceState(null, '', '/settings?tab=network');
    render(<Settings />);
    const topic = await screen.findByPlaceholderText('roofroof');
    fireEvent.input(topic, { target: { value: 'sqm/#' } });
    fireEvent.click(screen.getByRole('button', { name: /save/i }));
    expect(await screen.findByText(/with \/ between levels/)).toBeInTheDocument();
  });
});
