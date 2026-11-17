# Main search and model settings

The Library search bar keeps immediate filename/metadata filtering. When that
finds no photos, it searches photo content with the verified local SigLIP 2
encoder after a short typing pause. The text filter is removed before the
content-search snapshot is captured; project, date, rating and other filters
remain. Editing or clearing the query cancels the previous search. Progress
appears in the search bar and its existing result count.

The separate AI Search button, panel, and search setup popup are no longer
instantiated. Settings → Models now contains search-model verification,
download/import/pause, assistant model selection and connection/download
controls, and selection/object-removal model status. The latter tools retain
their supported compatible models. Agent model setup actions route to Settings.
A synchronous lazy-loader double-open that reset a first Settings request to
General was also fixed.

Direct app launches can find an already-installed search model in the native
launcher's shared application-data folder. Explicit model and isolated data
overrides remain authoritative, and full file hashing still gates use.

## Verification

- Four focused checks pass: Library search UI/coordinator, model setup, local
  similarity search, and catalog scope binding. The UI check also verifies that
  Settings model selection and file-check actions reach their controllers.
- The real frozen encoder passes `girl`, `green hat`, and mountain queries on
  four local photographs. `green hat` ranks the visibly matching photograph
  first. Repeated queries and worker restart reuse all four saved vectors.
- The native Cocoa proof loads production Main.qml, types into the main search
  bar with Qt keyboard events, clicks Clear, opens Settings → Models, checks the
  absence of the old search UI, and captures wide/compact layouts. All pass.
  Initial search/indexing took 25.8 s; warm searches took 1.35–1.77 s including
  the typing delay and UI work on this machine during a build.
- The Release app rebuild and final resource/link recipes pass. The resulting
  packaged executable reaches `qml.route.ready` in an offscreen smoke run with
  isolated data/cache folders and an intentionally unavailable catalog endpoint.
  The test process is stopped; existing editor sessions are not closed.

Evidence: `build/main-search-2026-10-28/contracts.log`, `real-inference.log`, and
`native-cocoa-fixed/receipt.json` with screenshots. These qualify the four-photo
workflow, not exhaustive retrieval accuracy or indexing speed for a full
library. Content results are ranked similarity candidates.
