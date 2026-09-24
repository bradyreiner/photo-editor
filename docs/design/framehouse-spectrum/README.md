# Framehouse Spectrum design study

Status: **rejected visual direction** following user review. Retain as a record of the exploration, not an implementation target. The user found these concepts weaker than the existing application. Further work must establish a stronger visual direction before using this prototype or specification as a baseline. The request for subtle caustic completion animations remains current.

Open `concept.html` in a browser. Everything is local; the adjacent reference images are required. The seven view tabs compare Edit, Selections, Compare, Analysis, Library, Export, and Color & motion. This study is a proposal, not the native application.

In **07 · Color & motion**, hover/focus the light and ink samples and click **Preview completion** on the adjustment, selection, and export samples to see the proposed one-shot caustic treatments. Use Reduced motion to compare the immediate-state fallback. Compact controls changes inspector density. Compare offers a draggable range control for the wipe study.

Read `design-spec.md` for the palette, control states, dimensions, information architecture, completion events, migration order, source seams, and native acceptance gates.

## Review evidence

- All seven views rendered in isolated headless Chrome and inspected as screenshots; see `*-concept.png`.
- `prototype-checks.json` records 16 passing checks at widths 1600, 1280, and 760: view switching, slider readout, thumbnail selection state, density, reduced motion, comparison wipe, operation selection, preview-only export, and horizontal page overflow.
- `caustic-checks.json` checks completion start, eventual settle, canvas cleanup, and persistent completion state. Cleanup has a timer fallback so suspended animation frames cannot leave the effect allocated indefinitely.
- JavaScript syntax checked with `node --check`.
- These checks cover the HTML study. They do not prove native QML layout, real photo processing, accessibility conformance, color accuracy, host GPU performance, or native completion timing. Scope traces and selection outline are illustrative. Export controls do not write files; slider changes update displayed values only.

The references are unmodified copies of user-supplied screenshots, with photograph regions framed in SVG. No native app source, catalog, or edit recipe was changed for this proposal.
