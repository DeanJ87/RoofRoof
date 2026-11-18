# Contract: onboarding components (`web/src/components/ui.tsx`)

The design system's onboarding set (constitution VII as amended by this spec). Each is built from
tokens only.

## `Modal`

```ts
Modal: FunctionalComponent<{
  labelledBy: string;          // id of the heading that names the dialog
  onEscape: () => void;        // Escape anywhere in the dialog (not consumed by a child)
  stopBar: ComponentChildren;  // required: the Stop bar (constitution VII), pass <RoofStopBar />
  header?: ComponentChildren;  // the bar across the top (Back, progress, Close)
  footer?: ComponentChildren;  // stays in view at the bottom
  aside?: ComponentChildren;   // side panel at >= 900 px, under the content below that
}>
```

- `role="dialog"`, `aria-modal="true"`, `aria-labelledby`.
- Tab and Shift+Tab wrap inside the dialog, Stop bar included.
- On mount, remembers the focused element; on unmount, focuses it if still in the document, else
  `#main`.
- Adds `has-modal` to `body` (no page scroll behind) while open.
- The caller makes the rest of the app inert (the layout does it for `/wizard`).

## `ProgressMeter` (extended)

New optional `valueText` → `aria-valuetext`.

## `ChoiceGroup`

```ts
ChoiceGroup: FunctionalComponent<{
  name: string;                // radio group name, unique on the page
  labelledBy: string;          // the question's id
  value: string | null;        // null: nothing selected yet
  onChange: (value: string) => void;
  options: { value: string; title: string; detail?: string; icon?: string; badge?: string }[];
}>
```

- A `role="radiogroup"` of native radio inputs inside label cards: arrows move and wrap, Space
  selects, one tab stop.
- `badge` renders as a cyan `Pill` (for "Recommended").
- Selected: cyan edge, tinted surface and a check mark.

## `SpeechBubble`

```ts
SpeechBubble: FunctionalComponent<{ id: string; children }>  // children: the question
```

- The mascot (decorative) and a bubble holding an `h2` with `id` and `tabIndex={-1}` (focused on
  each screen change).

## `Mascot`

```ts
Mascot: FunctionalComponent<{ size?: number }>
```

- An original dog head in token colours, `aria-hidden`, under 3 KB of markup.
