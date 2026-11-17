# Finding workflow problems before photographers do

The editor regression gate owns the required inventory in
`tests/fixtures/editor-regression/contract.json`. Repairs add their regressions
there in the same change. A missing, skipped or failing required test fails the
gate. Do not certify a workflow from a QML string describing its intended behavior.

## Automated exploration

`editor_authoring_sequence_test` explores combinations of layer, selection and
text creation, duplication, deletion, locking, reordering, adjustments and
Undo/Redo. Journeys of at least 20 steps begin with a deterministic coverage
prefix that must successfully apply all 18 mutation categories and reject a
locked-target edit. Eligible operations must change the recipe; API success
returns must agree with the observed mutation. Blocked operations must leave
the recipe unchanged. Per-operation and total
attempted/applied/rejected counts are saved with the seed. The gate rejects a
missing coverage report or a no-op-only run.

After each operation it checks unique layer/mask IDs, valid group parents and
counts, checks that Undo followed by Redo restores the exact recipe, and periodically reloads
the serialized recipe in a fresh controller. It uses an isolated temporary
runtime and never opens the photographer's library. It does not run model
inference or qualify native pointer behavior or photographic quality.

Build and run the required regression corpus:

```sh
cmake --preset dev-macos-debug
cmake --build build/dev-macos-debug --target editor_authoring_sequence_test --parallel 2
ctest --test-dir build/dev-macos-debug -R '^editor_authoring_sequence_test$' --output-on-failure
```

Explore the pinned seeds plus another deterministic seed derived from the current
commit, using the existing gate:

```sh
python3 -B scripts/perf/editor-regression-gate.py \
  --build-dir build/dev-macos-debug --tier exploration \
  --output /tmp/looksy-authoring-exploration
```

The output directory must be fresh. A failure report records the seed, requested
length, executable hash and exact replay command; the process log includes the
operation prefix through the first failing assertion. Replay or expand it with:

```sh
python3 -B scripts/perf/editor-regression-gate.py \
  --build-dir build/dev-macos-debug --tier exploration --seed 42 --steps 150 \
  --output /tmp/looksy-authoring-seed-42
```

The macOS CI job runs this exploration on each revision and keeps logs for 14
days. This configuration must land on the remote branch before remote CI can
execute it. Local runs against a dirty workspace remain local diagnostic proof,
not release qualification. Rebuild after source changes.

## What to add as workflows evolve

For every tool, cover entry, target choice, gestures, commit, cancel, Undo/Redo,
save, new-process reopen and export. Cross these with a locked/deleted target,
missing resources, delayed jobs, keyboard focus, document switches and window
sizes. Include the transitions between tools; those boundaries often expose
problems that isolated control tests miss.

Extend the sequence explorer's action set as stable authoring APIs become
available. Keep the checks about outcomes, not the implementation: unaffected
owners stay unchanged, canceled jobs cannot publish, drafts cannot steal another
target, and saved/exported content agrees with the accepted edit. Pin each
discovered failure as a small permanent regression before extending exploration.

Native fixtures must use the production controls, pointer and keyboard routes.
Capture real visible/enabled action groups, focus owner, target IDs and bounds;
check one active primary command group and no clipped actions, caret or handle
collisions. A component/controller pass cannot qualify the complete native path.
Use reviewed photographs and the packaged verified provider for SAM/inpaint
acceptance; deterministic fake providers remain separately labelled coverage.
The shell fixture requires `PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1` in addition to
`QT_QPA_PLATFORM=cocoa`; otherwise it deliberately runs offscreen. Native
authoring receipts must include an exposed Cocoa window and independently
qualified foreground sample. The validator rejects offscreen captures.
The existing native gate now requires separate `text_authoring`,
`selection_authoring` and `board_source` receipts, plus `real_inpaint` using the
verified LaMa provider through the production controller. The latter exercises
immediate Apply after a released brush stroke, final-quality patch publication,
Undo/Redo, controller reload/render parity and cancellation of regeneration. It
is model/controller evidence; it does not substitute for native pointer input,
fresh-process persistence, or a photographer reviewing the repair quality.
The Board journey uses `run-owned-board-workflow.py` to start and stop its own
temporary catalog and pass both Canvas/Board entry flags. Failure still stops
the owned service; failed service shutdown preserves its data and fails proof.

## Review loop

Each batch has an implementation owner and an independent workflow reviewer.
The reviewer follows task goals, probes alternative paths and failure recovery,
and checks recipes, images and persistence receipts. Findings identify the first
broken transition, a reproduction, severity and regression. Coding agents can
help explore and review; they do not replace a photographer's usability judgment.

Run short task-based reviews at feature boundaries, not only after a complaint.
Give the reviewer goals such as “refine the subject, add a title, make two Boards,
return to the photo and export”; do not supply the intended click sequence.
Confusion about the active target or next action is a UX defect even if each
button technically works. Track unqualified real-model, persistence and human
review cases explicitly in the authoring repair plan until their evidence exists.

Native preflight records macOS screen-lock telemetry. A locked desktop is host-unqualified before the native suite starts; unlock the test host and rerun. Foreground/exposure checks remain mandatory even when lock telemetry is unavailable. One-click Board selection and double-click editing are tested as different operations. Mutation tests must establish distinct starting states and prove that the named action changed them before testing Undo.
