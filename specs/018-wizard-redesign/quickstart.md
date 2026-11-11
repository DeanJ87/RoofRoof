# Quickstart: Setup Wizard Redesign

## Checks

```bash
(cd web && npm run typecheck && npm run test:coverage && npm run build)
(cd web && npm run build:demo && npx playwright test tests/wizard.spec.ts tests/wizard-a11y.spec.ts)
(cd web && npx playwright test)            # everything else, including a11y.spec.ts
python3 tools/ui/pack_data.py --dist web/dist --out data && python3 tools/ui/size_check.py --data data
PATH=~/Projects/SQMeter/.venv-quality/bin:$PATH python3 tools/quality/check.py
node tools/i18n/check.mjs && node tools/i18n/literals.mjs
mkdocs build --strict
```

## In the demo

1. Demo → Restart setup wizard: the dashboard dims and blurs; the dialog asks "What should we
   call this roof?" beside the dog.
2. Continue through the screens. On "How many park switches are fitted?" nothing is selected and
   Continue says to choose; pick One: the live state shows.
3. On a wide window the "Why this matters" panel sits to the right; at 320 px it is under the
   content, and the footer stays at the bottom.
4. Press Escape: "Leave setup?" with Keep going focused. Leave for now: the dashboard with the
   "Setup isn't finished" banner.
5. Start an open from the dashboard, open the wizard (banner → Continue setup): the Stop bar sits
   above the dialog's footer; Tab reaches it; click it.
6. Finish on the review: the done screen links the commissioning guide.
