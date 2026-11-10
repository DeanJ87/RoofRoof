# RoofRoof brand kit

The RoofRoof mark is a small observatory under a starry sky with its roof rolled off along the rails and the telescope out: the roof is the cyan part, because moving it is the whole job. (The name is what a roll-off roof says when it opens. The device itself is a safety controller, so the brand stays quiet.) Use the files here; don't redraw them.

Every SVG in `src/` is written by `tools/brand/build.mjs` from one drawing of the mark. To change the mark, edit that script and run it; don't edit the SVGs by hand.

| Folder | What's in it |
|---|---|
| `src/logo/` | The mark in every variant, the wordmark, and the lockups (source SVG) |
| `src/favicon/`, `src/app-icon/` | Favicons (16, 32, 48 px) and the 512 px app icon |
| `src/docs/` | The docs site's logo and favicon |
| `src/social/` | GitHub social card, README banner, square and story posts, forum header, release banner template |
| `src/sticker/` | Round sticker (60 mm) and enclosure label (70 × 24 mm), colour and one-colour |
| `png/` | Rendered PNGs the repo uses (README banner, social card, apple-touch icon) |
| `roofroof-tokens.css` | Brand colours, fonts and logo colours |

## Which mark, where

| Size or use | File |
|---|---|
| 40 px and larger (UI headers at 44 px, 40 px on phones; the docs header) | `roofroof-mark.svg` (detailed: gradient sky, more stars, lit roof edge, door) |
| 17 to 39 px (favicons, small icons) | `roofroof-mark-flat.svg` (flat colours, two stars) |
| 16 px | `favicon/favicon-16.svg` (fills the square, bolder shapes) |
| Red night-vision screens | `roofroof-mark-night.svg`, or `roofroof-mark-night-mono.svg` for one colour |
| One colour on screen | `roofroof-mark-mono.svg` (uses `currentColor`), `-mono-light`, `-mono-dark` |
| On cyan | `roofroof-mark-inverted.svg` |
| Print on light paper | `roofroof-mark-print.svg` |
| Silkscreen (PCB) | `roofroof-mark-silkscreen.svg` (white, no frame), at least 6 mm |
| Emboss, deboss, engraving | `roofroof-mark-emboss.svg`, at least 20 mm |
| The name alone | `roofroof-wordmark.svg` (light, for dark backgrounds), `roofroof-wordmark-dark.svg` |
| Name next to the mark | `roofroof-lockup-horizontal.svg` (`-light`, `-night`, `-mono`), at least 120 px wide; `roofroof-lockup-stacked.svg` |

## Rules

- **Clear space:** keep a quarter of the mark's height clear on every side.
- **Lockups:** the gap between mark and name is a third of the mark's height.
- **Grid:** 24 units; the tile is inset 2.25 units with a 2.75-unit corner radius (SQMeter's grid). Don't change the corners, add outlines or put the mark on a second tile.
- **Colour:** dark only. On light backgrounds use the print, mono or light-lockup variants, never the full-colour mark on white.
- **Don't** stretch, rotate, mirror (the roof rolls off to the right), recolour the roof, add effects (shadows, glows, gradients) or set the name in another font.
- **Name:** always "RoofRoof": capital R twice, one word. Not "Roof Roof" or "Roofroof".

## Colours and type

The brand colours are the app's colours (`web/src/index.css`, `docs/stylesheets/roofroof.css`), kept from SQMeter so the two look like one family: background `#080d14`, panel `#0d141d`, line `#1b2a38`, text `#e7edf3`, muted `#8b9baa`, cyan accent `#55c7f2`, and green, amber and red for safe, warning and unsafe. The values in `roofroof-tokens.css` match the app exactly; only some names differ (`--rr-accent` is `--cyan`, `--rr-ok`/`-warn`/`-bad` are green/amber/red, `--rr-line-2` is `--line-soft`). The app adds `--control-edge` (`#55687a`) so form controls meet the 3:1 contrast rule. The `--rr-logo-*` tokens are the colours `tools/brand/build.mjs` draws the mark with.

Type is Inter for text and JetBrains Mono for readings and code. The app and docs ship their own copies (`docs/assets/fonts/`).

## PNG and print files

The social, banner and sticker SVGs contain live text in Inter and JetBrains Mono, which GitHub, chat apps and print shops don't have. Render them with the project's own fonts:

```bash
cd web && npm ci && npx playwright install chromium && cd ..
node tools/brand/build.mjs                     # the SVGs, after changing the mark
node tools/brand/render.mjs                    # all of them into brand/render/ (git-ignored)
node tools/brand/render.mjs --only social      # social | icons | sticker
node tools/brand/render.mjs --release v0.1.0-beta.1 --date "10 November 2026" \
  --change "First change" --change "Second change" --change "Third change"
```

Social files come out at 1x and 2x. Stickers and labels come out as print PDFs at their real size (60 mm, 70 × 24 mm) and as 600 dpi PNGs. The enclosure label has L and R boxes to tick for the way the roof opens.

After rendering, copy the PNGs the repo uses into place:

| Rendered file | Copied to |
|---|---|
| `render/icons/apple-touch-icon-180.png` | `png/apple-touch-icon-180.png` |
| `render/icons/favicon-32.png` | `web/public/favicon-32.png` |
| `render/social/roofroof-social-1280x640.png` | `png/`, `docs/assets/social-card.png`, `.github/social-preview.png` |
| `render/social/roofroof-readme-banner.png`, `@2x` | `png/` |

`build.mjs` copies the SVGs itself (`src/docs/logo.svg` to `docs/assets/logo.svg` and `web/public/brand-mark.svg`, `src/docs/favicon.svg` to `docs/assets/favicon.svg`, `src/favicon/favicon.svg` to `web/public/favicon.svg`).

## Where the brand is used

| Place | Files |
|---|---|
| Device web UI header | `web/public/brand-mark.svg` (the detailed mark), via `web/src/components/BrandMark.tsx` |
| Device and demo favicon | `web/public/favicon.svg`, `web/public/favicon-32.png` |
| Demo site link preview and home-screen icon | `png/roofroof-social-1280x640.png`, `png/apple-touch-icon-180.png` (copied in by `web/vite.demo.config.ts`) |
| Docs logo, favicon, link preview | `docs/assets/logo.svg`, `docs/assets/favicon.svg`, `docs/assets/social-card.png` |
| README banner | `png/roofroof-readme-banner@2x.png` |
| Release banner | `src/social/roofroof-release-banner-template.svg`, filled in by `tools/release/notes.mjs` as `roofroof-release-<tag>.png` |
| GitHub social preview | `.github/social-preview.png`, uploaded by hand in the repository's Settings |
