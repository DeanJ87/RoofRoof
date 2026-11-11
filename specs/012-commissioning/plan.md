# Implementation Plan: Commissioning Guide

**Branch**: `spec/012-commissioning` | **Spec**: [spec.md](spec.md)

## Summary

Docs only. Two new pages (`docs/hardware/commissioning.md`, `commissioning-checklist.md`), a
commissioning section in troubleshooting, print styles in `docs/stylesheets/roofroof.css`, the
navigation, the README and a sweep of "comes in a later spec" wording once specs 008, 009 and 011 are
merged. One diagram (DIA-30, the stage order) with metadata.

## Technical Context

| | |
|---|---|
| **Language** | Markdown (MkDocs Material), Mermaid |
| **Testing** | `mkdocs build --strict`, `tools/docs/diagrams.py`, `tools/docs/test_ui_paths.py`, a check of every setting, route and log text the pages name against the merged code |
| **Constraints** | no firmware or UI change (so no demo core, flash or UI budget effect) |

## Approach

1. Collect the bench items (PRs #3 to #11) and the owner assumptions (specs 002 to 011) into
   [checklists/bench-trace.md](checklists/bench-trace.md).
2. Order them (safety order, spec.md clarifications) into the guide; write the checklist from the guide.
3. Steps that rest on unmerged specs (dashboard buttons and Roof tab, bench mode, MQTT topics) are
   written against the specs' contracts and marked in the trace file; after those merge, rebase and
   verify each such step against the merged code and docs.
4. Independent review of the procedure for safety order.

## Constitution check

IX: docs in the same change, diagram metadata. I-III: the order itself (the review in step 4 checks it).
No other principle is touched: no code changes.
