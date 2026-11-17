# Import browsing and review interactions

The first action now says Review N files. Confirmation names the actual admitted
batch, for example Import 24 photos. Folder and preset menu actions also describe
review rather than implying that they immediately start an import.

Filename search and thumbnail sizing preserve the selected batch. Shift-click or
Shift-Return adds the range between the anchor and current file in visible order.
Arrow keys move through the grid; Shift-arrow extends a range. Enter toggles a
file, and Space opens a large photo preview. The preview supports previous/next,
selection changes, and a return to the grid. It uses the existing original-image
provider; video files offer an explicit action to open the native player.

Selection updates no longer rebuild the complete media grid unless the Selected
filter requires a different list. This preserves focus and avoids unnecessary
thumbnail reloads. Search changes reset the range anchor without clearing picks.

Dropping local media files stages the exact supported file list. Dropping one
folder browses it without selecting or importing files automatically. Remote
URLs, unavailable files, and ambiguous mixtures of folders/files are rejected or
reported. The interface states the one-folder-at-a-time rule. Every admitted
batch still goes through the existing asynchronous preflight and explicit
confirmation.

Review retains a thumbnail strip of the selected batch. Location summaries show
readable names, with full paths available through Show full paths and exact-path
copy buttons. Copy feedback changes the icon to a check. A compact wavelength
progress strip replaces the large loading scene in the job summary. It uses
reported job progress and retains pause, resume, cancel, retry, and completed
receipt actions. Queued jobs no longer display fabricated partial completion.
Live per-file counts are unavailable from the current job API; completed-file
counts remain based on the worker's actual final receipt.

Native validation uses the real Main.qml window and isolated photographic
fixtures. It exercises native folder/file drops, rejection of remote URLs,
selection-preserving search and density, keyboard range selection, decoded large
previews and their selection/navigation controls, batch review, exact native
clipboard copy (restoring the prior clipboard afterward), managed/referenced
preflight, defaults, and optional project entry. Responsive captures cover
1440x900, 1280x760, and 900x700. Logs, captures, and source/binary hashes are under
`docs/evidence/import-workflow-2026-10-23`.

The fixture does not confirm a real catalog import. Completed-job receipt checks
use an injected job, so these results establish UI and preflight behavior rather
than end-to-end ingestion or release qualification.
