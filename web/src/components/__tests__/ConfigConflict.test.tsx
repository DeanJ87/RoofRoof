import { describe, expect, it } from 'vitest';
import { act, fireEvent, render, renderHook, screen, waitFor } from '@testing-library/preact';
import { http, HttpResponse } from 'msw';
import Settings from '../Settings';
import { Toaster } from '../toast';
import { server } from '../../test/mswServer';
import { mockDevice } from '../../test/mockDevice';
import { mockConfig } from '../../mocks/data';
import { useConfigEditor } from '../../hooks/useConfigEditor';
import { applyPatch } from '../../lib/configPatch';
import type { Config } from '../../types';

// Spec 021: two pages on one device. A device that keeps a revision and checks
// a save's base values field by field, as the firmware does (lib/ConfigModel ConfigSave).

type Node = Record<string, unknown>;
const isObject = (v: unknown): v is Node => typeof v === 'object' && v !== null && !Array.isArray(v);
const staleFields = (base: unknown, current: unknown, path = ''): string[] =>
  isObject(base)
    ? Object.entries(base).flatMap(([k, v]) => staleFields(v, isObject(current) ? current[k] : undefined, path ? `${path}.${k}` : k))
    : JSON.stringify(base ?? null) === JSON.stringify(current ?? null)
      ? []
      : [path];

const statefulDevice = (start: Config) => {
  const device = { config: start as unknown as Node, revision: 1000, bodies: [] as Node[] };
  mockDevice({ config: start });
  server.use(
    http.get('/api/config', () => HttpResponse.json(device.config, { headers: { 'X-Config-Revision': String(device.revision) } })),
    http.post('/api/config', async ({ request }) => {
      const { baseRevision, baseValues, ...patch } = (await request.json()) as Node;
      device.bodies.push({ baseRevision, baseValues, ...patch });
      if (baseRevision !== undefined && baseRevision !== device.revision) {
        const conflicts = staleFields(baseValues, device.config);
        if (conflicts.length > 0)
          return HttpResponse.json({ success: false, error: 'changed', conflicts, revision: device.revision }, { status: 409 });
      }
      device.config = applyPatch(device.config, patch);
      device.revision += 1;
      return HttpResponse.json({ success: true, revision: device.revision });
    }),
  );
  return device;
};

const twoEditors = async () => {
  const a = renderHook(() => useConfigEditor());
  const b = renderHook(() => useConfigEditor());
  await waitFor(() => expect(a.result.current.config && b.result.current.config).toBeTruthy());
  return { a: a.result, b: b.result };
};

const ninaUrl = (c: Node) => ((c.park as Node).nina as Node).url;

describe('two pages editing one device (spec 021)', () => {
  it('saves different fields from both pages, neither lost', async () => {
    const device = statefulDevice(mockConfig);
    const { a, b } = await twoEditors();
    act(() => a.current.applyChanges([[['park', 'nina', 'url'], 'http://nina:1888']]));
    expect((await a.current.save()).kind).toBe('saved');
    act(() => b.current.applyChanges([[['wifi', 'ipv6'], false]]));
    expect((await b.current.save()).kind).toBe('saved');
    expect(ninaUrl(device.config)).toBe('http://nina:1888');
    expect((device.config.wifi as Node).ipv6).toBe(false);
    expect(device.bodies[1]).toEqual({ baseRevision: 1000, baseValues: { wifi: { ipv6: true } }, wifi: { ipv6: false } });
  });

  it('refuses the older page for the same field, and Reload keeps its other edits', async () => {
    const device = statefulDevice(mockConfig);
    const { a, b } = await twoEditors();
    act(() => a.current.applyChanges([[['park', 'nina', 'url'], 'http://theirs']]));
    await a.current.save();
    act(() =>
      b.current.applyChanges([
        [['park', 'nina', 'url'], 'http://mine'],
        [['deviceName'], 'obs'],
      ]),
    );
    const outcome = await b.current.save();
    expect(outcome).toEqual({ kind: 'conflict', fields: ['park.nina.url'] });
    expect(ninaUrl(device.config)).toBe('http://theirs');
    let replaced: string[] | null = null;
    await act(async () => {
      replaced = await b.current.rebase(['park.nina.url']);
    });
    expect(replaced).toEqual(['park.nina.url']);
    expect(b.current.config!.park!.nina.url).toBe('http://theirs');
    expect(b.current.config!.deviceName).toBe('obs');
    expect(b.current.dirty).toBe(true);
    expect((await b.current.save()).kind).toBe('saved');
    expect(device.config.deviceName).toBe('obs');
  });

  it("never saves a Wi-Fi password against a network changed elsewhere, and drops it on Reload", async () => {
    const device = statefulDevice(mockConfig);
    const { b } = await twoEditors();
    device.config = { ...device.config, wifi: { ...(device.config.wifi as Node), ssid: 'NewNet' } };
    device.revision += 1; // Wi-Fi setup saved from another page
    act(() => b.current.applyChanges([[['wifi', 'password'], 'typed-for-old']]));
    const outcome = await b.current.save();
    expect(outcome).toEqual({ kind: 'conflict', fields: ['wifi.ssid'] });
    await act(async () => {
      expect(await b.current.rebase(['wifi.ssid'])).toEqual(['wifi.password']);
    });
    expect(b.current.config!.wifi.ssid).toBe('NewNet');
    expect(b.current.dirty).toBe(false);
  });

  it("keeps its own revision after its own save, the old one when another save came between", async () => {
    const device = statefulDevice(mockConfig);
    const { a, b } = await twoEditors();
    act(() => a.current.applyChanges([[['deviceName'], 'one']]));
    await a.current.save();
    act(() => a.current.applyChanges([[['deviceName'], 'two']]));
    await a.current.save();
    expect(device.bodies[1].baseRevision).toBe(1001);
    act(() => b.current.applyChanges([[['wifi', 'ipv6'], false]]));
    await b.current.save();
    act(() => b.current.applyChanges([[['wifi', 'mdns'], !mockConfig.wifi.mdns]]));
    await b.current.save();
    // Another save came between b's load and its first save: b keeps checking field by field.
    expect(device.bodies[3].baseRevision).toBe(1000);
    expect(device.bodies[3].baseValues).toEqual({ wifi: { mdns: mockConfig.wifi.mdns } });
  });

  it('a device without a revision gets a plain partial save', async () => {
    mockDevice();
    let sent: Node | null = null;
    server.use(
      http.post('/api/config', async ({ request }) => {
        sent = (await request.json()) as Node;
        return HttpResponse.json({ success: true });
      }),
    );
    const { result } = renderHook(() => useConfigEditor());
    await waitFor(() => expect(result.current.config).toBeTruthy());
    act(() => result.current.applyChanges([[['wifi', 'ipv6'], false]]));
    await result.current.save();
    expect(sent).toEqual({ wifi: { ipv6: false } });
  });

  it('Settings says what changed and Reload shows it', async () => {
    const device = statefulDevice(mockConfig);
    window.history.replaceState(null, '', '/settings?tab=device');
    render(<Toaster />);
    render(<Settings />);
    const name = await waitFor(() => {
      const el = document.querySelector<HTMLInputElement>('[data-field="deviceName"]');
      if (!el) throw new Error('not yet');
      return el;
    });
    device.config = { ...device.config, deviceName: 'elsewhere' };
    device.revision += 1;
    fireEvent.input(name, { target: { value: 'here' } });
    fireEvent.change(name, { target: { value: 'here' } });
    fireEvent.click(await screen.findByRole('button', { name: 'Save' }));
    expect(await screen.findByText(/Settings changed on the device since this page loaded them: deviceName/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Reload' }));
    expect(await screen.findByText(/These now show the device's values: deviceName/)).toBeInTheDocument();
    await waitFor(() => expect(document.querySelector<HTMLInputElement>('[data-field="deviceName"]')!.value).toBe('elsewhere'));
  });
});
