import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';
import type { Plugin } from 'vite';

// Spec 026: RoofRoof ships English only, and the device sends its text in
// English, so the device.* catalogue in en.json (tools/i18n/gen_device_catalog.py)
// only maps English to the same English. It stays in en.json for the checks and
// for a future language, but the built page leaves out every device.* message
// no page names in its own code: deviceText() then shows the device's text as
// sent, which is the same words, and the device's LittleFS keeps the space.

const SOURCE = join(__dirname, 'src');
const NAMED = /['"`](device\.[\w.]+)['"`]/g;

/** Every device.* key the page's own code names (tests and en.json aside). */
const namedKeys = (dir = SOURCE, found = new Set<string>()): Set<string> => {
  for (const entry of readdirSync(dir)) {
    const path = join(dir, entry);
    if (entry === '__tests__' || entry === 'i18n') continue;
    if (statSync(path).isDirectory()) namedKeys(path, found);
    else if (/\.tsx?$/.test(entry)) for (const match of readFileSync(path, 'utf8').matchAll(NAMED)) found.add(match[1]);
  }
  return found;
};

export const englishOnly = (): Plugin => ({
  name: 'roofroof-english-only',
  apply: 'build',
  enforce: 'pre',
  transform(code, id) {
    if (!id.endsWith('/src/i18n/en.json')) return null;
    const named = namedKeys();
    const messages = JSON.parse(code) as Record<string, unknown>;
    const kept = Object.fromEntries(Object.entries(messages).filter(([key]) => !key.startsWith('device.') || named.has(key)));
    return { code: JSON.stringify(kept), map: null };
  },
});
