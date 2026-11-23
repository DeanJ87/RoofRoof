#!/usr/bin/env node
// Writes the RoofRoof brand SVGs (brand/src/) and the copies the web UI and the
// docs use, from the one drawing of the mark below. Edit the mark here, run
// this, then `node tools/brand/render.mjs` for the PNGs.
//
//   node tools/brand/build.mjs
//
// The mark: RoofRoof's dog (the name is the bark a roll-off roof makes when it
// opens), the web UI's mascot exactly as drawn, under a starry sky on a dark
// tile. 24-unit grid, the tile inset 2.25 units with a 2.75-unit corner radius
// (the same grid as SQMeter's mark). One dog at every size; the variants change
// only its colours and the tile behind it.

import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const SRC = join(ROOT, 'brand/src');

// Colours: brand/roofroof-tokens.css.
export const C = {
  bg: '#080d14',
  line: '#1b2a38',
  line2: '#142130',
  text: '#e7edf3',
  muted: '#8b9baa',
  dim: '#768798',
  accent: '#55c7f2',
  ok: '#5ee0a0',
  skyTop: '#0d1724',
  skyHorizon: '#25394d',
  tileEdge: '#2a3d52',
  dogFur: '#b9c7d4',
  dogMuzzle: '#dfe7ee',
  dogEar: '#5f7184',
  dogTag: '#f4c15d',
  printInk: '#0d141d',
  printAccent: '#0b78a6',
  night: '#ff4a3a',
};

const SANS = 'Inter, system-ui, sans-serif';
const MONO = "'JetBrains Mono', ui-monospace, monospace";

// --- The drawing -------------------------------------------------------------

// The tile every full-colour mark sits on: 24-unit grid, inset 2.25 units, 2.75-unit corners.
const TILE = { x: 2.25, y: 2.25, w: 19.5, h: 19.5, r: 2.75 };

// The dog: the web UI's mascot (web/src/components/ui.tsx, `Mascot`, DS-13),
// path for path, in its 64-unit drawing. Every mark uses all of it, at every
// size; only the colours and the tile behind it change.
// web/src/components/__tests__/Onboarding.test.tsx checks the two still match.
export const DOG = {
  ears: 'M17 15c-7 1-11 11-9 21 1 6 7 6 9 1l5-16zM47 15c7 1 11 11 9 21-1 6-7 6-9 1l-5-16z',
  head: 'M32 12c11 0 18 8 18 19 0 12-8 19-18 19s-18-7-18-19c0-11 7-19 18-19z',
  patch: 'cx="24.5" cy="28" rx="5.5" ry="6"',
  muzzle: 'cx="32" cy="39" rx="9.5" ry="7"',
  eyeLeft: 'cx="24.5" cy="29" r="2.3"',
  eyeRight: 'cx="39.5" cy="29" r="2.3"',
  nose: 'M28.5 35h7c0 2.2-1.6 3.6-3.5 3.6s-3.5-1.4-3.5-3.6z',
  mouth: 'M32 38.6v2.6m-4 1c1.6 1.3 3 1.3 4 0 1 1.3 2.4 1.3 4 0',
  collar: 'M20 47c7 4.5 17 4.5 24 0',
  tag: 'm32 50 1.3 2.7 3 .4-2.2 2 .6 3-2.7-1.5-2.7 1.5.6-3-2.2-2 3-.4z',
};

// The mascot's colours (web/src/index.css: --mascot-*, --bg, --cyan, --amber).
const MASCOT = { ear: C.dogEar, fur: C.dogFur, muzzle: C.dogMuzzle, eye: C.bg, collar: C.accent, tag: C.dogTag };

// The dog in full colour, in the 64-unit drawing.
function dog(k = MASCOT) {
  return (
    `<path d="${DOG.ears}" fill="${k.ear}"/>` +
    `<path d="${DOG.head}" fill="${k.fur}"/>` +
    `<ellipse ${DOG.patch} fill="${k.ear}"/>` +
    `<ellipse ${DOG.muzzle} fill="${k.muzzle}"/>` +
    `<g fill="${k.eye}"><circle ${DOG.eyeLeft}/><circle ${DOG.eyeRight}/><path d="${DOG.nose}"/></g>` +
    `<path d="${DOG.mouth}" fill="none" stroke="${k.eye}" stroke-width="1.4" stroke-linecap="round"/>` +
    `<path d="${DOG.collar}" fill="none" stroke="${k.collar}" stroke-width="3" stroke-linecap="round"/>` +
    `<path d="${DOG.tag}" fill="${k.tag}"/>`
  );
}

// The same dog in one ink: the shapes filled, and the patch, the eye on it, the
// other eye, the nose, the mouth, a line round the muzzle and thin gaps between
// the head, ears, collar and tag cut out, so every feature shows in a single
// colour. `accent` colours the collar and tag (print, inverted).
function dogMono(p, color, accent = color) {
  return (
    `<mask id="${p}m" maskUnits="userSpaceOnUse" x="0" y="0" width="64" height="64">` +
    `<path d="${DOG.ears}" fill="#fff"/>` +
    `<path d="${DOG.head}" fill="#fff" stroke="#000" stroke-width="2.4" paint-order="stroke"/>` +
    `<ellipse ${DOG.patch} fill="#000"/><circle ${DOG.eyeLeft} fill="#fff"/>` +
    `<ellipse ${DOG.muzzle} fill="none" stroke="#000" stroke-width="1.2"/>` +
    `<g fill="#000"><circle ${DOG.eyeRight}/><path d="${DOG.nose}"/></g>` +
    `<path d="${DOG.mouth}" fill="none" stroke="#000" stroke-width="1.4" stroke-linecap="round"/>` +
    `<path d="${DOG.collar}" fill="none" stroke="#000" stroke-width="6" stroke-linecap="round"/>` +
    `<path d="${DOG.tag}" fill="#000" stroke="#000" stroke-width="2.4" stroke-linejoin="round"/>` +
    `</mask>` +
    `<rect width="64" height="64" fill="${color}" mask="url(#${p}m)"/>` +
    `<path d="${DOG.collar}" fill="none" stroke="${accent}" stroke-width="3" stroke-linecap="round"/>` +
    `<path d="${DOG.tag}" fill="${accent}"/>`
  );
}

// Where the 64-unit dog sits on the 24-unit tile.
const ON_TILE = 'translate(.48 -.6) scale(.36)';
const tileRect = (fill) => `<rect x="${TILE.x}" y="${TILE.y}" width="${TILE.w}" height="${TILE.h}" rx="${TILE.r}" fill="${fill}"/>`;

// A few stars in the night sky behind the dog: the observatory it guards.
const STARS =
  `<circle cx="4.6" cy="4.8" r=".5" opacity=".95"/><circle cx="7.6" cy="3.6" r=".28" opacity=".55"/>` +
  `<circle cx="3.8" cy="9.6" r=".26" opacity=".5"/><circle cx="19.2" cy="4.6" r=".42" opacity=".85"/>` +
  `<circle cx="16.4" cy="3.4" r=".24" opacity=".5"/><circle cx="20.2" cy="9.2" r=".24" opacity=".45"/>`;

// The full mark: the dog under a starry sky, a faint cyan glow behind its head.
export function markDetailed(p = 'rr') {
  return (
    `<defs><clipPath id="${p}c"><rect x="${TILE.x}" y="${TILE.y}" width="${TILE.w}" height="${TILE.h}" rx="${TILE.r}"/></clipPath>` +
    `<linearGradient id="${p}s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${C.skyTop}"/><stop offset="1" stop-color="${C.skyHorizon}"/></linearGradient>` +
    `<radialGradient id="${p}h" cx="12" cy="12.5" r="8" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="${C.accent}" stop-opacity=".22"/><stop offset="1" stop-color="${C.accent}" stop-opacity="0"/></radialGradient>` +
    `</defs>` +
    `<g clip-path="url(#${p}c)">` +
    `<rect x="2.25" y="2.25" width="19.5" height="19.5" fill="url(#${p}s)"/>` +
    `<g fill="${C.text}">${STARS}</g>` +
    `<circle cx="12" cy="12.5" r="8" fill="url(#${p}h)"/>` +
    `<g transform="translate(1.6 1.7) scale(.325)">${dog()}</g>` +
    `</g>` +
    `<rect x="2.4" y="2.4" width="19.2" height="19.2" rx="2.6" fill="none" stroke="${C.tileEdge}" stroke-width=".3"/>`
  );
}

// The same dog, bigger, on a plain tile: small icons (no stars to turn to specks).
export function markFlat() {
  return tileRect(C.skyTop) + `<g transform="${ON_TILE}">${dog()}</g>`;
}

// Favicons: the dog filling a plain tile that fills the square (64-unit grid).
// The mascot's own colours: its ear colour stands apart from both the head
// and the tile at 16 px.
export function markSmall() {
  return (
    `<rect width="64" height="64" rx="12" fill="${C.skyTop}"/>` +
    `<g transform="translate(-4.8 -8.25) scale(1.15)">${dog()}</g>`
  );
}

// The app icon (home screen, PWA): the dog on the dark starry tile filling the
// square. `rounded` false leaves the corners square for platforms that round
// them (maskable PWA icons, the Apple touch icon); the dog stays in the middle
// 80 % either way.
export function appIcon(p = 'ra', { rounded = true } = {}) {
  return (
    `<defs><linearGradient id="${p}s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${C.skyTop}"/><stop offset="1" stop-color="${C.skyHorizon}"/></linearGradient>` +
    `<radialGradient id="${p}h" cx="12" cy="12.6" r="9" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="${C.accent}" stop-opacity=".22"/><stop offset="1" stop-color="${C.accent}" stop-opacity="0"/></radialGradient></defs>` +
    `<rect width="24" height="24"${rounded ? ' rx="3.4"' : ''} fill="url(#${p}s)"/>` +
    `<g fill="${C.text}"><circle cx="4.4" cy="4.6" r=".5" opacity=".95"/><circle cx="7.6" cy="3" r=".28" opacity=".55"/><circle cx="19.4" cy="4.4" r=".42" opacity=".85"/>` +
    `<circle cx="16.6" cy="2.8" r=".24" opacity=".5"/><circle cx="2.9" cy="10" r=".26" opacity=".5"/><circle cx="21.2" cy="9.6" r=".24" opacity=".45"/></g>` +
    `<circle cx="12" cy="12.6" r="9" fill="url(#${p}h)"/>` +
    `<g transform="translate(1.6 1.9) scale(.325)">${dog()}</g>`
  );
}

// One colour: the tile as a frame, the dog in one ink. For currentColor,
// single-ink print, silkscreen and emboss. `frame` false drops the frame and
// draws the dog larger (silkscreen at small sizes reads better without it).
export function markMono(p, color = 'currentColor', { frame = true } = {}) {
  const place = frame ? 'translate(3.04 2.2) scale(.28)' : 'translate(-.8 -2) scale(.4)';
  return (
    (frame ? `<rect x="3" y="3" width="18" height="18" rx="2.5" fill="none" stroke="${color}" stroke-width="1.5"/>` : '') +
    `<g transform="${place}">${dogMono(p, color)}</g>`
  );
}

// Dark dog on the accent: the tile is cyan, the dog ink, the collar and tag white.
export function markInverted(p = 'ri') {
  return tileRect(C.accent) + `<g transform="${ON_TILE}">${dogMono(p, C.printInk, '#ffffff')}</g>`;
}

// Light paper: white tile with an ink frame, the dog in ink, a darker cyan collar and tag.
export function markPrint(p = 'rp') {
  return (
    tileRect('#ffffff') +
    `<g transform="translate(2.4 1.95) scale(.3)">${dogMono(p, C.printInk, C.printAccent)}</g>` +
    `<rect x="2.75" y="2.75" width="18.5" height="18.5" rx="2.25" fill="none" stroke="${C.printInk}" stroke-width="1"/>`
  );
}

// Night vision: the dog in reds only, on a plain tile.
export function markNight() {
  const tile = '#1a0806';
  return tileRect(tile) + `<g transform="${ON_TILE}">${dog({ ear: '#8a2a20', fur: '#c23a2c', muzzle: '#e0503f', eye: tile, collar: C.night, tag: C.night })}</g>`;
}

// --- Files -----------------------------------------------------------------

const svg = (w, h, body, vb = `0 0 ${w} ${h}`) => `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="${vb}">${body}</svg>\n`;
const place = (x, y, size, body, vb = '0 0 24 24') => `<svg x="${x}" y="${y}" width="${size}" height="${size}" viewBox="${vb}">${body}</svg>`;
const text = (x, y, size, content, { fill = C.text, weight, family = SANS, spacing, anchor } = {}) =>
  `<text x="${x}" y="${y}" font-family="${family}" font-size="${size}"${weight ? ` font-weight="${weight}"` : ''}${spacing !== undefined ? ` letter-spacing="${spacing}"` : ''}${anchor ? ` text-anchor="${anchor}"` : ''} fill="${fill}">${content}</text>`;
const name = (x, y, size, opts = {}) => text(x, y, size, 'RoofRoof', { weight: 600, spacing: -(size / 100).toFixed(2) * 1, ...opts });
const label = (x, y, content, opts = {}) => text(x, y, 15, content, { fill: C.muted, weight: 600, spacing: 1.4, ...opts });

// The roof-state readout on the social cards: what the device is for.
function roofStatus(x, y, scale = 1) {
  const s = (n) => +(n * scale).toFixed(2);
  return (
    label(x, y, 'ROOF') +
    text(x - s(4), y + s(120), s(112), 'OPEN', { fill: C.accent, weight: 500, family: MONO, spacing: -s(3) }) +
    text(x, y + s(166), s(24), 'Parked · rails clear · rain dry', { fill: C.muted }) +
    `<line x1="${x}" y1="${y + s(214)}" x2="${x + s(352)}" y2="${y + s(214)}" stroke="${C.line2}" stroke-width="2"/>` +
    label(x, y + s(262), 'SAFE') +
    text(x, y + s(298), s(28), 'Yes', { fill: C.ok, family: MONO }) +
    label(x + s(120), y + s(262), 'MOTOR') +
    text(x + s(120), y + s(298), s(28), 'Off', { family: MONO }) +
    label(x + s(252), y + s(262), 'ALPACA') +
    text(x + s(252), y + s(298), s(28), 'Up', { family: MONO })
  );
}

export function files() {
  const out = {};
  const D = (p) => markDetailed(p);
  const add = (path, content) => (out[path] = content);

  // Logo
  add('logo/roofroof-mark.svg', svg(24, 24, D('rm')));
  add('logo/roofroof-mark-flat.svg', svg(24, 24, markFlat()));
  add('logo/roofroof-mark-mono.svg', svg(24, 24, markMono('rmm', 'currentColor')));
  add('logo/roofroof-mark-mono-light.svg', svg(24, 24, markMono('rml', C.text)));
  add('logo/roofroof-mark-mono-dark.svg', svg(24, 24, markMono('rmd', C.printInk)));
  add('logo/roofroof-mark-night.svg', svg(24, 24, markNight()));
  add('logo/roofroof-mark-night-mono.svg', svg(24, 24, markMono('rmnm', C.night)));
  add('logo/roofroof-mark-inverted.svg', svg(24, 24, markInverted('rmi')));
  add('logo/roofroof-mark-print.svg', svg(24, 24, markPrint('rmp')));
  add('logo/roofroof-mark-silkscreen.svg', svg(24, 24, markMono('rms', '#ffffff', { frame: false })));
  add('logo/roofroof-mark-emboss.svg', svg(24, 24, markMono('rme', '#000000')));
  add('logo/roofroof-wordmark.svg', svg(160, 40, name(0, 30, 30)));
  add('logo/roofroof-wordmark-dark.svg', svg(160, 40, name(0, 30, 30, { fill: C.printInk })));
  add('logo/roofroof-lockup-horizontal.svg', svg(216, 48, place(0, 0, 48, D('rl')) + name(64, 34, 30)));
  add('logo/roofroof-lockup-horizontal-light.svg', svg(216, 48, place(0, 0, 48, markPrint('rll')) + name(64, 34, 30, { fill: C.printInk })));
  add('logo/roofroof-lockup-horizontal-night.svg', svg(216, 48, place(0, 0, 48, markNight()) + name(64, 34, 30, { fill: C.night })));
  add('logo/roofroof-lockup-horizontal-mono.svg', svg(216, 48, place(0, 0, 48, markMono('rlm', 'currentColor')) + name(64, 34, 30, { fill: 'currentColor' })));
  add('logo/roofroof-lockup-stacked.svg', svg(160, 136, place(40, 0, 80, D('rs')) + name(80, 124, 30, { anchor: 'middle' })));

  // Icons
  add('favicon/favicon-16.svg', svg(16, 16, markSmall(), '0 0 64 64'));
  add('favicon/favicon-32.svg', svg(32, 32, markSmall(), '0 0 64 64'));
  add('favicon/favicon-48.svg', svg(48, 48, D('f48'), '2.25 2.25 19.5 19.5'));
  add('favicon/favicon.svg', svg(32, 32, markSmall(), '0 0 64 64'));
  add('app-icon/roofroof-app-icon-512.svg', svg(512, 512, appIcon('ra'), '0 0 24 24'));
  add('app-icon/roofroof-app-icon-maskable-512.svg', svg(512, 512, appIcon('rq', { rounded: false }), '0 0 24 24'));

  // Docs (copied to docs/assets/)
  add('docs/logo.svg', svg(24, 24, D('dl')));
  add('docs/favicon.svg', svg(32, 32, markSmall(), '0 0 64 64'));

  // Social
  const frame = (w, h, rx) =>
    `<rect width="${w}" height="${h}" rx="${rx}" fill="${C.bg}"/>` + (rx ? `<rect x="1" y="1" width="${w - 2}" height="${h - 2}" rx="${rx}" fill="none" stroke="${C.line}" stroke-width="2"/>` : '');
  add(
    'social/roofroof-social-1280x640.svg',
    svg(
      1280,
      640,
      frame(1280, 640, 0) +
        place(96, 96, 112, D('so')) +
        name(96, 320, 76) +
        text(98, 374, 26, 'Roll-off roof controller for the ESP32.', { fill: C.muted }) +
        text(98, 414, 26, 'Native ASCOM Alpaca. Fails closed.', { fill: C.muted }) +
        text(98, 544, 22, 'github.com/DeanJ87/RoofRoof', { fill: C.dim, family: MONO }) +
        roofStatus(784, 152),
    ),
  );
  add(
    'social/roofroof-readme-banner.svg',
    svg(
      1280,
      320,
      frame(1280, 320, 16) +
        place(80, 72, 176, D('rb')) +
        name(300, 150, 68) +
        text(302, 202, 26, 'Roll-off roof controller for the ESP32.', { fill: C.muted }) +
        text(302, 250, 20, 'Motor · park · rails · rain · ASCOM Alpaca · MQTT', { fill: C.dim, family: MONO }) +
        text(1200, 150, 84, 'OPEN', { fill: C.accent, weight: 500, family: MONO, spacing: -2, anchor: 'end' }) +
        text(1200, 190, 20, 'roof · parked', { fill: C.muted, anchor: 'end' }),
    ),
  );
  add(
    'social/roofroof-forum-header-1200x480.svg',
    svg(
      1200,
      480,
      frame(1200, 480, 0) +
        place(80, 72, 72, D('fh')) +
        name(170, 124, 36) +
        text(80, 290, 46, 'An open roll-off roof controller', { weight: 600, spacing: -0.8 }) +
        text(80, 346, 46, 'for the ESP32.', { weight: 600, spacing: -0.8 }) +
        text(80, 410, 22, 'Motor · park · rails · rain · ASCOM Alpaca · MQTT', { fill: C.dim, family: MONO }) +
        text(1120, 250, 120, 'OPEN', { fill: C.accent, weight: 500, family: MONO, spacing: -4, anchor: 'end' }) +
        text(1120, 300, 22, 'roof · parked', { fill: C.muted, anchor: 'end' }),
    ),
  );
  add(
    'social/roofroof-post-square-1080.svg',
    svg(
      1080,
      1080,
      frame(1080, 1080, 0) +
        place(96, 96, 96, D('ps')) +
        name(212, 162, 48) +
        label(96, 420, 'ROOF · LIVE') +
        text(86, 640, 240, 'OPEN', { fill: C.accent, weight: 500, family: MONO, spacing: -8 }) +
        text(96, 712, 36, 'Parked · rails clear · rain dry', { fill: C.muted }) +
        `<line x1="96" y1="800" x2="984" y2="800" stroke="${C.line}" stroke-width="2"/>` +
        text(96, 872, 42, 'Your observatory roof, under control.', { weight: 600, spacing: -0.6 }) +
        text(96, 928, 32, 'Opens on command. Closes when it has to.', { fill: C.muted }) +
        text(96, 1000, 24, 'github.com/DeanJ87/RoofRoof', { fill: C.dim, family: MONO }),
    ),
  );
  add(
    'social/roofroof-post-story-1080x1920.svg',
    svg(
      1080,
      1920,
      frame(1080, 1920, 0) +
        place(340, 220, 400, D('pt')) +
        name(540, 770, 76, { anchor: 'middle' }) +
        label(540, 1040, 'ROOF · LIVE', { anchor: 'middle' }) +
        text(540, 1250, 240, 'OPEN', { fill: C.accent, weight: 500, family: MONO, spacing: -8, anchor: 'middle' }) +
        text(540, 1326, 36, 'Parked · rails clear · rain dry', { fill: C.muted, anchor: 'middle' }) +
        text(540, 1540, 42, 'No cloud. Fails closed.', { weight: 600, anchor: 'middle' }) +
        text(540, 1600, 42, 'Native ASCOM Alpaca.', { weight: 600, anchor: 'middle' }) +
        text(540, 1760, 30, 'github.com/DeanJ87/RoofRoof', { fill: C.dim, family: MONO, anchor: 'middle' }),
    ),
  );
  // Placeholders "v1.4.0", "Released …" and "Change one/two/three" are filled
  // in by tools/brand/render.mjs (renderReleaseBanner).
  add(
    'social/roofroof-release-banner-template.svg',
    svg(
      1280,
      320,
      frame(1280, 320, 16) +
        place(80, 64, 48, D('rt')) +
        text(144, 98, 26, 'RoofRoof firmware', { weight: 600, spacing: -0.3 }) +
        text(78, 214, 96, 'v1.4.0', { fill: C.accent, weight: 500, family: MONO, spacing: -3 }) +
        text(82, 262, 22, 'Released 9 October 2026', { fill: C.muted }) +
        `<line x1="720" y1="64" x2="720" y2="256" stroke="${C.line2}" stroke-width="2"/>` +
        label(768, 104, 'IN THIS RELEASE') +
        text(768, 150, 22, 'Change one') +
        text(768, 190, 22, 'Change two') +
        text(768, 230, 22, 'Change three') +
        text(768, 268, 18, 'github.com/DeanJ87/RoofRoof/releases', { fill: C.dim, family: MONO }),
    ),
  );

  // Stickers and labels: 10 units per millimetre.
  add(
    'sticker/roofroof-sticker-round-60mm.svg',
    svg(
      600,
      600,
      `<circle cx="300" cy="300" r="300" fill="${C.bg}"/><circle cx="300" cy="300" r="270" fill="none" stroke="${C.line}" stroke-width="3"/>` +
        place(170, 96, 260, D('st')) +
        name(300, 430, 60, { anchor: 'middle' }) +
        text(300, 488, 22, 'roll-off roof controller', { fill: C.dim, family: MONO, anchor: 'middle' }),
    ),
  );
  add(
    'sticker/roofroof-label-enclosure-70x24mm.svg',
    svg(
      700,
      240,
      `<rect width="700" height="240" rx="20" fill="${C.bg}"/><rect x="10" y="10" width="680" height="220" rx="12" fill="none" stroke="${C.line}" stroke-width="2"/>` +
        place(30, 30, 180, D('le')) +
        name(240, 104, 48) +
        text(242, 144, 20, 'Roll-off roof controller', { fill: C.muted }) +
        text(242, 196, 16, 'S/N ________    FW ______', { fill: C.dim, family: MONO }) +
        `<line x1="568" y1="40" x2="568" y2="200" stroke="${C.line2}" stroke-width="2"/>` +
        // Which way the roof opens, ticked by whoever installs it.
        label(630, 92, 'OPENS', { anchor: 'middle' }) +
        `<rect x="592" y="116" width="24" height="24" rx="4" fill="none" stroke="${C.muted}" stroke-width="2"/>` +
        `<rect x="644" y="116" width="24" height="24" rx="4" fill="none" stroke="${C.muted}" stroke-width="2"/>` +
        text(604, 172, 16, 'L', { fill: C.muted, family: MONO, anchor: 'middle' }) +
        text(656, 172, 16, 'R', { fill: C.muted, family: MONO, anchor: 'middle' }),
    ),
  );
  add(
    'sticker/roofroof-label-enclosure-mono.svg',
    svg(
      700,
      240,
      `<rect x="4" y="4" width="692" height="232" rx="18" fill="none" stroke="#000" stroke-width="4"/>` +
        place(30, 30, 180, markMono('lem', '#000')) +
        name(240, 104, 48, { fill: '#000' }) +
        text(242, 144, 20, 'Roll-off roof controller', { fill: '#000' }) +
        text(242, 196, 16, 'S/N ________    FW ______', { fill: '#000', family: MONO }),
    ),
  );
  return out;
}

// The copies other parts of the repo serve, by path.
export const COPIES = {
  'docs/logo.svg': ['docs/assets/logo.svg', 'web/public/brand-mark.svg'],
  'docs/favicon.svg': ['docs/assets/favicon.svg'],
  'favicon/favicon.svg': ['web/public/favicon.svg'],
};

function main() {
  for (const [path, content] of Object.entries(files())) {
    const file = join(SRC, path);
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(file, content);
    for (const copy of COPIES[path] ?? []) writeFileSync(join(ROOT, copy), content);
    console.log(`brand/src/${path}${COPIES[path] ? ` → ${COPIES[path].join(', ')}` : ''}`);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) main();
