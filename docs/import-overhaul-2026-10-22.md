# Import UI overhaul

Import now separates source selection and photographic review from the final
destination review. The media grid gets the main canvas, with a compact source
rail, full-frame previews, explicit selection marks, keyboard focus, a persistent
selection count, and one primary import action. Paths wrap, and unsupported
previews retain a format or video fallback.

The final review puts the project target, managed-copy versus leave-in-place
behavior, source and destination paths, backup status, and scan readiness first.
Presets, organization, and RAW/JPEG policies live under More options in the Studio
workflow. Confirmation stays visible in a fixed footer while the review scrolls.
The source and grid panels return when the review closes.

Folder models use an unavailable location until a source is active, preventing
an empty source URL from showing the process working directory. Individual file
selections retain their exact file list and do not expose unrelated folder
navigation. Shared source, folder, and media controls use the neutral application
theme and preserve their existing activation APIs.

Three Luna agents at xhigh handled the layout, shared controls, and test audit.
Integration included parser checks, native photographic review, and corrections
for panel ownership, source-rail spacing, selection feedback, and fixture/frame
settling in the native capture path.

Native proof uses the real Main.qml shell and temporary copies of three
repository photographs. It checks decoded previews, keyboard selection, managed
and referenced preflight, confirmation disabled while scanning, and responsive
bounds at 1440x900, 1280x760, and 900x700. The ordinary preflight shard also checks
multi-folder picks, policy refresh, and a synthetic completed-job receipt.
Screenshots and logs are in `docs/evidence/import-overhaul-2026-10-22`.

Validation: the desktop app build passed, the Import-specific source guard
passed, and the final native Import preflight shard passed in 16.65 seconds with
nine redesign captures plus the existing completed-receipt captures. The native
test rejected warnings from ImportView and its four shared Import components.

This is UI and preflight proof. The fixture uses an unavailable catalog database
and does not confirm a real import. The completed-job receipt is injected, so it
does not establish end-to-end catalog ingestion, backup, or export completion.

Separate checks remain failing in the concurrent tree: the broad source audit
and import-safety shard stop on Timeline contract assertions; the standalone
catalog preflight test fails background job polling's heartbeat check. Its
sandboxed run also crashed inside the database endpoint socket probe during job
history polling. These failures are separate from the native Import page proof.
