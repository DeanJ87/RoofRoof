# Research: Setup Wizard Redesign

## R1. Design direction

The wizard is used once, usually at the bench or in the observatory at dusk, on a phone as often
as a laptop. The design keeps RoofRoof's night palette and spends its one bold move on the mascot
and its speech bubble; everything else is quiet.

**Tokens** (all already in `index.css` unless marked new):

| Role | Token | Value |
|---|---|---|
| Night behind everything | `--bg` | `#080d14` |
| Dialog surface | `--panel` | `#0d141d` |
| Choice surface, bubble | `--panel-2` | `#111b27` |
| Selection, progress, focus | `--cyan` | `#55c7f2` |
| Backdrop dim (new) | `--scrim` | `rgba(3, 6, 10, 0.62)`, solid `rgba(3, 6, 10, 0.92)` without blur |
| Mascot fur (new) | `--mascot-fur` | `#b9c7d4`, a moonlit grey-blue |
| Mascot ears and patch (new) | `--mascot-ear` | `#5f7184` |
| Mascot tag | `--amber` | `#f4c15d` |
| Dialog radius (new) | `--radius-dialog` | `1rem`; choice cards keep the card radius `0.75rem`, icon tiles `0.6rem` |

**Type**: Inter RR, the UI's only face (no new font, no size budget for one). The question in the
bubble is 1.2 rem, weight 600, line height 1.3; choice titles 0.95 rem/600; details 0.82 rem in
`--muted`. No capitals for labels, no eyebrow above the question.

**Layout**:

```text
desktop ≥ 900 px                                 phone ≤ 599 px
┌──────────────────────────────────────────────┐  ┌──────────────────────┐
│ ←   ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░   ✕  │  │ ← ▓▓▓▓░░░░░░░░░░░  ✕ │
├──────────────────────────────┬───────────────┤  │ 🐕 ╭───────────────╮ │
│ 🐕 ╭────────────────────────╮ │ Why this      │  │    │ How many park │ │
│    │ How many park switches │ │ matters       │  │    │ switches?     │ │
│    │ are fitted?            │ │ ...           │  │    ╰───────────────╯ │
│    ╰────────────────────────╯ │               │  │ ┌──────────────────┐ │
│ ┌──────────────────────────┐ │               │  │ │▢ None          ○ │ │
│ │▢ None             detail ○│ │               │  │ └──────────────────┘ │
│ └──────────────────────────┘ │               │  │ ...                  │
│ ┌──────────────────────────┐ │               │  │ Why this matters ... │
│ │▢ One    [Recommended]   ●│ │               │  ├──────────────────────┤
│ └──────────────────────────┘ │               │  │ Skip for now  [Cont.]│
├──────────────────────────────┴───────────────┤  ├──────────────────────┤
│ Skip for now                      [Continue] │  │ ■ Opening   [ Stop ] │ (only while moving)
└──────────────────────────────────────────────┘  └──────────────────────┘
```

Left-aligned throughout (reading order, RTL mirrors through logical properties). Choices are a
single column of full-width rows, not a grid of tiles: each is a question's answer, read top to
bottom.

**Principles**: one question per screen; the answer is the biggest thing on it; the selected
choice is marked by an edge, a tint and a check, never by colour alone; one motion (the screen's
content fades in), none with reduced motion; the dog is decoration, the words carry the meaning.

**Review against the generic defaults**: the first plan had a big "Step 3 of 20" eyebrow in
capitals over the question and a 2×2 grid of choice tiles with identical radius and shadow. Both
are template habits: the eyebrow goes (the progress bar and its text alternative say where the
owner is), and the choices become a single column of rows with a radius hierarchy (dialog > card
> icon). The first mascot idea was a dog with a telescope; a dog with a star tag on its collar is
simpler, reads at 48 px and needs fewer paths.

## R2. Where the dialog lives

- Decision: `/wizard` renders the dashboard as the page, and the wizard as a modal sibling of the
  main region. The header, the skip link and the main region get `inert` while it is open.
- Rationale: one address keeps spec 015's links (System, banner, demo, `?step=`); `inert` gives the
  "rest of the app unreachable" half of a modal without a portal library (preact/compat isn't in
  the bundle).
- Alternatives: a portal into `body` (needs `preact/compat`, about 3 KB); the native `<dialog>`
  element with `showModal()` (its top layer would sit above the Stop bar unless the Stop bar is
  inside it, and jsdom doesn't implement it, so every unit test would need a polyfill).

## R3. Stop over the dialog

- Decision: `Modal` takes a required `stopBar` slot; the wizard passes `<RoofStopBar />`, and the
  layout leaves its own Stop bar out while a modal is open. The bar is `position: fixed` inside the
  dialog element; nothing between it and the viewport has a transform, filter or backdrop filter
  (they would make it fixed to that element instead), so the blur sits on a sibling backdrop.
- Rationale: `aria-modal` hides everything outside the dialog from screen readers and the focus
  trap keeps Tab inside it, so a Stop bar outside the dialog would be visible but unreachable by
  keyboard and silent to screen readers. Inside, it is part of the loop.
- The dialog's height is `100dvh - var(--stop-bar)` on phones, and its footer sits above the bar on
  desktop (`--stop-bar` is already measured by `RoofStopBar`).

## R4. Choice cards

- Decision: native radio inputs, visually hidden, inside label cards, in a `role="radiogroup"`
  labelled by the question. Browsers give arrow keys that move and wrap, Space to select, one tab
  stop for the group, and the right announcements.
- Alternatives: `role="radio"` buttons with a roving tab index (more code, the same behaviour);
  rejected for size and risk.
- With no choice selected, Tab enters the group on its first choice and arrows select (browser
  behaviour); a test pins it in Chromium.

## R5. Progress

- Decision: extend `ProgressMeter` with `valueText` (`aria-valuetext`, "Screen 3 of 20") instead of
  adding a second progress component; the wizard's bar is the same component with a slimmer style
  in the dialog header.

## R6. Focus

- Trap: a keydown handler on the dialog wraps Tab and Shift+Tab between the first and last
  focusable elements inside it (the Stop bar included), and pulls focus back in if it ever lands
  outside.
- Each screen change focuses the question (`tabIndex=-1` heading in the bubble).
- Restore: the element focused when the dialog opened is kept; on close, focus goes back to it if
  it is still in the document, otherwise to the main region. Because the wizard opens by a route
  change, the opener (System's button, the banner's) is usually gone by then, so the wizard routes
  back to the page it was opened from and the main region takes focus.

## R7. Escape

- Escape on the dialog opens "Leave setup?". A "?" tip already stops its own Escape from
  propagating, so Escape inside a tip only closes the tip.

## R8. Blur fallback

- `@supports (backdrop-filter: blur(1px)) or (-webkit-backdrop-filter: blur(1px))` adds the blur
  over the translucent scrim; without it the scrim is nearly opaque.

## R9. Size

- The old page list, action row and per-page cards go; the screens reuse the Settings fields. The
  new components are about 2 KB of JS and 1.5 KB of CSS gzipped; the screen texts about 2 KB.
  Target: at most 6 KB growth, recorded in the size baseline with its reason.

## R10. Light and dark

- RoofRoof has no light theme (none in SQMeter either). `:root` gains `color-scheme: dark` so a
  light-preferring browser draws its own controls dark too. Tests run every screen under both
  `prefers-color-scheme` values.
