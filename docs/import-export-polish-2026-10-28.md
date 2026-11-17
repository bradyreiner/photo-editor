# Import and Export refinement

Import uses a compact source rail and a photo grid with one filename per tile.
Review uses a large photograph next to a focused collection/copy/readiness
column. Narrow windows keep the controls in a scrolling column and retain the
primary action in the footer. The secondary contact sheet is optional. Source,
destination, and full paths are disclosed through Details rather than repeated
across the review page. Storage opens a compact two-row dialog with optional
locations and path copy; duplicate comparison identifies files by filename.

Export gives the photograph most of the space and groups format, size, and
destination in a narrow settings column. Filename, color space, and crop are
under More options. Active jobs have one thin aggregate progress bar and compact
rows; failed jobs reserve enough height for the actual error and recovery
controls. The main navigation does not repeat transfer progress already shown by
the owning page. Routine image loading uses a corner spinner.

The import/export backends, preflight requirements, copy policies, durable job
history, and existing retry/cancel commands remain connected to their original
controllers. Single-photo submission still runs fresh checks when a preliminary
report is being refreshed; blocked reports and invalid destinations prevent
admission.

## Evidence and boundaries

Artifacts are in `build/transfer-polish-2026-10-28/`.

- `final-contract-3.log`: source-contract and import-safety checks pass, 2/2.
- `import-verified/`: 21 native Cocoa captures with real decoded photographs,
  selection and keyboard interaction, managed/referenced review, full-path copy,
  preview/duplicate/storage dialogs, 90/100/200% interface scales, and compact
  windows.
- `export-verified/`: native ready/advanced/running/failed layouts at 1440, 1280,
  and 900 px widths, with a real decoded photograph. Running/failure records are
  deliberately simulated job-history fixtures. These checks validate UI and
  preflight behavior, not completed file delivery.
- `release-final-build-2.log`: packaged Release resource generation, compilation,
  linking, and agent/search runtime verification pass. Two stale render objects
  from concurrent rendering changes were rebuilt to resolve signature mismatch.
- `packaged-smoke.json`: the packaged app reaches `qml.route.ready` with isolated
  data/cache directories; the test stops only its own process.
- `source-binary-sha256.json`: relevant final QML and executable hashes.

The global popup inventory flags four existing stock-tooltip violations in
`SceneWorkspace.qml`; it reports none in the changed transfer UI. The popup audit
regression suite passes. No existing editor process or user catalog was closed
or altered for this proof.
