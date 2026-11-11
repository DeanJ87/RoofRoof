import { describe, it, expect } from 'vitest';
import { getConfigValidationErrors } from '../validation/configSchema';
import { mockConfig } from '../mocks/data';

describe('MQTT config validation', () => {
  const withMqtt = (mqtt: Record<string, unknown>) =>
    getConfigValidationErrors({ ...mockConfig, mqtt: { ...mockConfig.mqtt, enabled: true, broker: '192.168.1.5', ...mqtt } });

  it('rejects wildcards or a trailing slash in the base topic', () => {
    expect(withMqtt({ topic: 'roofroof/#' })['mqtt.topic']).toMatch(/letters, numbers/);
    expect(withMqtt({ topic: 'roofroof/' })['mqtt.topic']).toMatch(/letters, numbers/);
    expect(withMqtt({ topic: '/roofroof' })['mqtt.topic']).toMatch(/letters, numbers/);
    expect(withMqtt({ topic: 'roofroof+x' })['mqtt.topic']).toMatch(/letters, numbers/);
    expect(withMqtt({ topic: 'roofroof/roof' })['mqtt.topic']).toBeUndefined();
  });

  it('checks the discovery prefix only when discovery is on', () => {
    expect(
      withMqtt({ topic: 'roofroof', homeAssistant: { enabled: false, discoveryPrefix: '' } })['mqtt.homeAssistant.discoveryPrefix'],
    ).toBeUndefined();
    expect(
      withMqtt({ topic: 'roofroof', homeAssistant: { enabled: true, discoveryPrefix: '' } })['mqtt.homeAssistant.discoveryPrefix'],
    ).toBeDefined();
  });
});
