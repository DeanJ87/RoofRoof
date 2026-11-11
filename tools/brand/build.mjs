#!/usr/bin/env node
// Writes the RoofRoof brand SVGs (brand/src/) and the copies the web UI and the
// docs use, from the one drawing of the mark below. Edit the mark here, run
// this, then `node tools/brand/render.mjs` for the PNGs.
//
//   node tools/brand/build.mjs
//
// The mark: a small observatory under a starry sky, its roof rolled off along
// the rails to the right and the telescope out. 24-unit grid, the tile inset
// 2.25 units with a 2.75-unit corner radius (the same grid as SQMeter's mark).

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
  accentCore: '#d6f3ff',
  ok: '#5ee0a0',
  skyTop: '#0d1724',
  skyHorizon: '#25394d',
  ground: '#13212f',
  groundLow: '#0b141d',
  wall: '#8b9baa',
  wallShade: '#5d6e7f',
  scope: '#e7edf3',
  flatTile: '#1d2f41',
  tileEdge: '#2a3d52',
  printInk: '#0d141d',
  printAccent: '#0b78a6',
  night: '#ff4a3a',
};

const SANS = 'Inter, system-ui, sans-serif';
const MONO = "'JetBrains Mono', ui-monospace, monospace";

// --- The drawing (24 × 24 units) --------------------------------------------

const TILE = { x: 2.25, y: 2.25, w: 19.5, h: 19.5, r: 2.75 };
const HORIZON = 18.5;
// Building: the walls stop at the rail; the rail runs on past them to a post.
const WALL = { x: 4, y: 12.4, w: 8.6, h: HORIZON - 12.4 };
const RAIL = { x: 4, y: 11.4, w: 16.5, h: 1 };
const POST = { x: 18.9, y: 12.4, w: 1.2, h: HORIZON - 12.4 };
// The roof, rolled off: a low gable sitting on the rail beyond the walls.
const ROOF = 'M12.1 11.4V9.9L16.3 7.5L20.5 9.9V11.4Z';
// Telescope tube, from inside the walls up to the stars at the top left.
const SCOPE = { x1: 8.6, y1: 11.6, x2: 6.1, y2: 7.2 };

const rect = (r, fill, extra = '') => `<rect x="${r.x}" y="${r.y}" width="${r.w}" height="${r.h}"${r.r ? ` rx="${r.r}"` : ''} fill="${fill}"${extra}/>`;
const tube = (fill, width) => `<path d="M${SCOPE.x1} ${SCOPE.y1}L${SCOPE.x2} ${SCOPE.y2}" fill="none" stroke="${fill}" stroke-width="${width}" stroke-linecap="round"/>`;

function clip(id, inset = TILE) {
  return `<clipPath id="${id}"><rect x="${inset.x}" y="${inset.y}" width="${inset.w}" height="${inset.h}" rx="${inset.r}"/></clipPath>`;
}

// Detailed, full colour: 40 px and larger.
export function markDetailed(p = 'rr') {
  return (
    `<defs>${clip(`${p}c`)}` +
    `<linearGradient id="${p}s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${C.skyTop}"/><stop offset=".85" stop-color="${C.skyHorizon}"/></linearGradient>` +
    `<linearGradient id="${p}g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${C.ground}"/><stop offset="1" stop-color="${C.groundLow}"/></linearGradient>` +
    `<linearGradient id="${p}w" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="${C.wall}"/><stop offset="1" stop-color="#71818f"/></linearGradient>` +
    `<linearGradient id="${p}r" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7fd6f6"/><stop offset="1" stop-color="${C.accent}"/></linearGradient>` +
    `<radialGradient id="${p}h" cx="8.3" cy="12" r="4.2" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="${C.accent}" stop-opacity=".35"/><stop offset="1" stop-color="${C.accent}" stop-opacity="0"/></radialGradient>` +
    `</defs>` +
    `<g clip-path="url(#${p}c)">` +
    `<rect x="2.25" y="2.25" width="19.5" height="19.5" fill="url(#${p}s)"/>` +
    `<g fill="${C.text}">` +
    `<circle cx="4.9" cy="5" r=".55" opacity=".95"/><circle cx="8.4" cy="3.9" r=".32" opacity=".6"/>` +
    `<circle cx="3.9" cy="8.6" r=".3" opacity=".5"/><circle cx="11.3" cy="5.6" r=".4" opacity=".75"/>` +
    `<circle cx="14.6" cy="3.8" r=".28" opacity=".5"/><circle cx="18.9" cy="5" r=".45" opacity=".8"/>` +
    `<circle cx="10" cy="8.4" r=".22" opacity=".4"/><circle cx="20.6" cy="2.9" r=".22" opacity=".4"/>` +
    `</g>` +
    `<circle cx="8.3" cy="12" r="4.2" fill="url(#${p}h)"/>` +
    `<path d="M2.25 ${HORIZON}Q8 ${HORIZON - 0.35} 12 ${HORIZON}T21.75 ${HORIZON - 0.2}V21.75H2.25Z" fill="url(#${p}g)"/>` +
    tube(C.scope, 1.7) +
    `<path d="M7.2 9.15L6.5 7.9" stroke="${C.accentCore}" stroke-width="1.7" stroke-linecap="butt" opacity=".9"/>` +
    rect(WALL, `url(#${p}w)`) +
    `<rect x="6.9" y="14.6" width="2.4" height="${HORIZON - 14.6}" fill="${C.wallShade}"/>` +
    rect(POST, '#71818f') +
    rect(RAIL, C.muted) +
    `<path d="${ROOF}" fill="url(#${p}r)"/>` +
    `<path d="M12.1 9.9L16.3 7.5L20.5 9.9" fill="none" stroke="${C.accentCore}" stroke-width=".35" stroke-linejoin="round"/>` +
    `</g>` +
    `<rect x="2.4" y="2.4" width="19.2" height="19.2" rx="2.6" fill="none" stroke="${C.tileEdge}" stroke-width=".3"/>`
  );
}

// Flat full colour: 17 to 39 px. Same shapes, no gradients, two stars.
export function markFlat(p = 'rf', tile = C.flatTile) {
  return (
    `<defs>${clip(`${p}c`)}</defs>` +
    `<g clip-path="url(#${p}c)">` +
    `<rect x="2.25" y="2.25" width="19.5" height="19.5" fill="${tile}"/>` +
    `<circle cx="4.9" cy="5" r=".7" fill="${C.muted}"/><circle cx="11.2" cy="5.2" r=".6" fill="${C.muted}"/>` +
    `<path d="M2.25 ${HORIZON}H21.75V21.75H2.25Z" fill="${C.ground}"/>` +
    tube(C.scope, 1.8) +
    rect(WALL, C.wall) +
    rect(POST, C.wall) +
    rect(RAIL, C.wall) +
    `<path d="${ROOF}" fill="${C.accent}"/>` +
    `</g>`
  );
}

// 16 px: fills more of the square, fewer and bolder shapes.
export function mark16(p = 'r16') {
  const t = { x: 0.75, y: 0.75, w: 22.5, h: 22.5, r: 3.5 };
  return (
    `<defs>${clip(`${p}c`, t)}</defs>` +
    `<g clip-path="url(#${p}c)">` +
    `<rect x="0" y="0" width="24" height="24" fill="${C.flatTile}"/>` +
    `<path d="M0 18.5H24V24H0Z" fill="${C.ground}"/>` +
    `<path d="M8.2 11.5L5.6 6.6" fill="none" stroke="${C.scope}" stroke-width="2.6" stroke-linecap="round"/>` +
    `<rect x="2.25" y="12.5" width="9.5" height="6" fill="${C.wall}"/>` +
    `<rect x="2.25" y="11" width="20" height="1.5" fill="${C.wall}"/>` +
    `<rect x="19.2" y="12.5" width="1.6" height="6" fill="${C.wall}"/>` +
    `<path d="M11.5 11V9.2L16.6 6.2L21.7 9.2V11Z" fill="${C.accent}"/>` +
    `</g>`
  );
}

// One colour: the tile as a frame, the drawing filled. For currentColor, single
// ink print, silkscreen and emboss. `frame` false drops the frame (silkscreen
// at small sizes reads better without it).
export function markMono(color = 'currentColor', { frame = true } = {}) {
  // The drawing scaled into the frame, on a ground line.
  const g = frame ? ' transform="translate(12 12) scale(.8) translate(-12 -12.6)"' : '';
  return (
    (frame ? `<rect x="3" y="3" width="18" height="18" rx="2.5" fill="none" stroke="${color}" stroke-width="1.5"/>` : '') +
    `<g${g}>` +
    `<circle cx="4.9" cy="5" r=".8" fill="${color}"/><circle cx="11.2" cy="5.2" r=".7" fill="${color}"/>` +
    tube(color, 1.9) +
    rect(WALL, color) +
    rect(POST, color) +
    rect(RAIL, color) +
    `<path d="${ROOF}" fill="${color}"/>` +
    `<rect x="3" y="${HORIZON}" width="18" height="1" fill="${color}"/>` +
    `</g>`
  );
}

// Dark drawing on the accent: the tile is cyan, the shapes are ink.
export function markInverted(p = 'ri') {
  return (
    `<defs>${clip(`${p}c`)}</defs>` +
    `<g clip-path="url(#${p}c)">` +
    `<rect x="2.25" y="2.25" width="19.5" height="19.5" fill="${C.accent}"/>` +
    `<circle cx="4.9" cy="5" r=".7" fill="${C.printInk}"/><circle cx="11.2" cy="5.2" r=".6" fill="${C.printInk}"/>` +
    `<path d="M2.25 ${HORIZON}H21.75V21.75H2.25Z" fill="${C.printInk}"/>` +
    tube(C.printInk, 1.8) +
    rect(WALL, C.printInk) +
    rect(POST, C.printInk) +
    rect(RAIL, C.printInk) +
    `<path d="${ROOF}" fill="#ffffff"/>` +
    `</g>`
  );
}

// Light paper: white tile with an ink frame, ink drawing, a darker cyan roof.
export function markPrint(p = 'rp') {
  return (
    `<defs>${clip(`${p}c`)}</defs>` +
    `<g clip-path="url(#${p}c)">` +
    `<rect x="2.25" y="2.25" width="19.5" height="19.5" fill="#ffffff"/>` +
    `<circle cx="4.9" cy="5" r=".7" fill="${C.printInk}"/><circle cx="11.2" cy="5.2" r=".6" fill="${C.printInk}"/>` +
    `<path d="M2.25 ${HORIZON}H21.75V21.75H2.25Z" fill="${C.printInk}"/>` +
    tube(C.printInk, 1.8) +
    rect(WALL, C.printInk) +
    rect(POST, C.printInk) +
    rect(RAIL, C.printInk) +
    `<path d="${ROOF}" fill="${C.printAccent}"/>` +
    `</g>` +
    `<rect x="2.75" y="2.75" width="18.5" height="18.5" rx="2.25" fill="none" stroke="${C.printInk}" stroke-width="1"/>`
  );
}

// Night vision: the flat mark in reds only.
export function markNight(p = 'rn') {
  return (
    `<defs>${clip(`${p}c`)}</defs>` +
    `<g clip-path="url(#${p}c)">` +
    `<rect x="2.25" y="2.25" width="19.5" height="19.5" fill="#1a0806"/>` +
    `<circle cx="4.9" cy="5" r=".7" fill="#8a2a20"/><circle cx="11.2" cy="5.2" r=".6" fill="#8a2a20"/>` +
    `<path d="M2.25 ${HORIZON}H21.75V21.75H2.25Z" fill="#2a0c08"/>` +
    tube('#c23a2c', 1.8) +
    rect(WALL, '#8a2a20') +
    rect(POST, '#8a2a20') +
    rect(RAIL, '#8a2a20') +
    `<path d="${ROOF}" fill="${C.night}"/>` +
    `</g>`
  );
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
  add('logo/roofroof-mark-flat.svg', svg(24, 24, markFlat('rmf')));
  add('logo/roofroof-mark-mono.svg', svg(24, 24, markMono('currentColor')));
  add('logo/roofroof-mark-mono-light.svg', svg(24, 24, markMono(C.text)));
  add('logo/roofroof-mark-mono-dark.svg', svg(24, 24, markMono(C.printInk)));
  add('logo/roofroof-mark-night.svg', svg(24, 24, markNight('rmn')));
  add('logo/roofroof-mark-night-mono.svg', svg(24, 24, markMono(C.night)));
  add('logo/roofroof-mark-inverted.svg', svg(24, 24, markInverted('rmi')));
  add('logo/roofroof-mark-print.svg', svg(24, 24, markPrint('rmp')));
  add('logo/roofroof-mark-silkscreen.svg', svg(24, 24, markMono('#ffffff', { frame: false })));
  add('logo/roofroof-mark-emboss.svg', svg(24, 24, markMono('#000000')));
  add('logo/roofroof-wordmark.svg', svg(160, 40, name(0, 30, 30)));
  add('logo/roofroof-wordmark-dark.svg', svg(160, 40, name(0, 30, 30, { fill: C.printInk })));
  add('logo/roofroof-lockup-horizontal.svg', svg(216, 48, place(0, 0, 48, D('rl')) + name(64, 34, 30)));
  add('logo/roofroof-lockup-horizontal-light.svg', svg(216, 48, place(0, 0, 48, markPrint('rll')) + name(64, 34, 30, { fill: C.printInk })));
  add('logo/roofroof-lockup-horizontal-night.svg', svg(216, 48, place(0, 0, 48, markNight('rln')) + name(64, 34, 30, { fill: C.night })));
  add('logo/roofroof-lockup-horizontal-mono.svg', svg(216, 48, place(0, 0, 48, markMono('currentColor')) + name(64, 34, 30, { fill: 'currentColor' })));
  add('logo/roofroof-lockup-stacked.svg', svg(160, 136, place(40, 0, 80, D('rs')) + name(80, 124, 30, { anchor: 'middle' })));

  // Icons
  add('favicon/favicon-16.svg', svg(16, 16, mark16('f16'), '0 0 24 24'));
  add('favicon/favicon-32.svg', svg(32, 32, markFlat('f32'), '2.25 2.25 19.5 19.5'));
  add('favicon/favicon-48.svg', svg(48, 48, markFlat('f48'), '2.25 2.25 19.5 19.5'));
  add('favicon/favicon.svg', svg(32, 32, markFlat('fsv'), '2.25 2.25 19.5 19.5'));
  add('app-icon/roofroof-app-icon-512.svg', svg(512, 512, D('ra'), '2.25 2.25 19.5 19.5'));

  // Docs (copied to docs/assets/)
  add('docs/logo.svg', svg(24, 24, D('dl')));
  add('docs/favicon.svg', svg(32, 32, markFlat('df'), '2.25 2.25 19.5 19.5'));

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
        place(30, 30, 180, markMono('#000')) +
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
