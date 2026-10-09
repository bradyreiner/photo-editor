# Agent access and long-running reversible tasks

User objective: the agent can access all app functions and perform long-running
tasks that can be canceled and reverted. Posters, YouTube thumbnails, and ads are
acceptance workflows; they do not replace the full app scope.

The agreed prompt/questions/plan/Start/result interaction and optional retro
terminal presentation are specified in
[the conversational experience plan](agent-simple-terminal-experience-plan.md).
Its phases extend this execution plan while keeping backend complexity out of
the primary user flow. The first complete workflow is an existing-photo thumbnail;
blank posters, wedding batches, ads and full app coverage remain in scope.

## Requirements and evidence

- [ ] Inventory every user-facing command and editable control across Library,
  Edit, Carousel, Timeline, Scene, Export, and app preferences. Record each
  command's typed arguments, capability, resource ownership, async completion,
  cancellation, and reversal. Existing UI functionality must be reachable;
  discover missing engine functionality separately.
- [ ] Use a shared command registry for discovery and dispatch. Expose typed
  schemas and real document/layer IDs. Preserve license, scope, and capability
  checks. App lifecycle and QObject internals are not user editing commands.
- [ ] Capture resource baselines before the first mutation. Persist operation
  acceptance and completion separately. Stop pending work and prevent late
  callbacks from altering restored documents. Revert the entire task, including
  composition steps; never overwrite unrelated manual changes.
- [x] Replace the eight-call/six-round whole-job limit with asynchronous
  continuation. Bound individual batches, detect repeated unproductive actions,
  and keep working while making progress without elapsed-time cutoffs.
  The existing photo planner now retains an eight-call batch admission limit,
  continues without a total call/round limit, and detects repeated results.
  `evidence/agent-long-tasks-2026-10-06/` contains the 79-call controller fixture,
  cancellation beyond the former limit, and repeated-result checks. This does
  not establish all-native-tool cancellation or real-model design quality.
- [ ] Persist task checkpoints and completed command IDs. Support recovery and
  resumption without repeating accepted mutations after interruption/relaunch.
- [ ] Refresh rendered visual evidence after changes. Maintain compact current
  document state and a task ledger rather than dropping all evidence at a size
  limit. Let review and repair use the same editing tools as initial construction.
- [ ] Route Stop and Revert through the same task ownership/cancellation system
  in the real UI. Keep operation status and recovery failures visible.
- [ ] Verify a long workflow beyond previous step limits, Stop during native
  async processing, whole-task reversal, exact before/after save/reopen behavior,
  and preservation of subsequent unrelated edits.
- [ ] Verify unrestricted local-model creation and revision of a thumbnail,
  poster, and ad through the actual app, with editable layers and exported pixels.
  Component tests and guided fixtures are insufficient for this requirement.

## Current implementation baseline

Automatic work deadlines were removed in
`evidence/agent-unlimited-waits-2026-10-06/`. The total eight-call/six-round limits
have now been replaced by productive continuation. Remaining work includes
recipe-only automatic repair, incomplete domain ownership, and in-memory
planner continuation. Many existing photo design controls are now connected,
while blank-canvas creation and other native domains remain unowned. Native import/audio/video work still
has its own deadlines, as recorded in `agent-complex-edit-blockers-2026-10-06.md`.

The shared native command registry now connects typed discovery and dispatch
through MCP. Its first enabled resource owner is the open photo: native shapes,
fills, source images, grouping, typography, layer effects and selected image
controls use the existing photo journal. Stable layer IDs replace UI indices;
read-only inspection has a separate read-capability route.

The first Timeline document adapter now uses the same task journal as the photo,
with stable clip IDs and explicit native document commands. Library, Carousel,
Scene, Export, preferences and jobs remain discoverable with unavailable reasons
until their resource owners are implemented. Standalone Timeline task admission
and native heavy-job ownership also remain pending. The production guard requires
the Timeline ID in the task targets; ordinary photo tasks cannot mutate an
unrelated open Timeline. Blank-canvas creation, complete control coverage and
QML-only workflows remain pending, so the registry requirement is not checked off.

Safe tool-error correction now returns certified unapplied rejections to the
planner, preserves completed mutations, withholds dependent calls and retains
failure evidence. Repeated errors stop through the existing progress guard;
cancel/revert and unresolved-error checks pass. Native transform and text
perspective no-ops now use verified readback. Twenty focused/regression checks
and the production build pass in `evidence/agent-tool-correction-2026-10-06/`.
This does not complete full-tool visual repair or resource coverage.

The shared coordinator now owns photo and the first native Timeline document
adapter in one journal. Exact mixed-document reversal, manual/closed-owner
conflicts, native pending-paint cancellation, save/reopen and recreated-owner
recovery pass. Completion checks current document checkpoints and production
Timeline dispatch requires the Timeline ID in task targets. Twenty-five selected
checks pass across the regression and corrected asynchronous export-receipt
fixture; final coordinator/MCP guard reruns and the production build pass in
`evidence/agent-multi-document-2026-10-06/`. Current source/binary hashes remain
unchanged after those final checks. This does not check off complete resource
coverage, real UI target admission/relaunch, Timeline render quality or actual
local-model design acceptance.

Synchronous draft runs now acquire the same task ownership as queued entry,
and kept fast drafts finalize their native task before clearing async state.
Final catalog application preflights enrolled native resources; failed native
finalization remains visible in the report and persisted manifest. Six focused
checks and 16 regression checks pass in
`evidence/agent-entry-ownership-2026-10-06/`; affected targets compile/link and
source/binary hashes remain unchanged after tests. This covers the controller
entry/finalization contracts, not all catalog/file owners or native UI behavior.
Whole-draft revisions now queue a new request on the original task, keep the
current composition and original target IDs, await pending pixels and retain the
first baseline for whole-task cancellation/Revert. Reviewed/interrupted tasks
must have current checkpoints and no incomplete operations before continuation.
Five focused revision fixtures and the native mixed-document continuation check
pass in `evidence/agent-revision-continuation-2026-10-06/`. Finding/point corrections,
sample review, alternative restoration and additional catalog owners still need
coverage; these checks do not establish native UI or real-model design quality.

The photo construction planner now renders the live recipe before initial and
follow-up model turns in a request-owned background stage. Required image
evidence carries recipe and PNG hashes, is checked against the live document,
and cannot fall back to the original photo when unavailable. Each completed
round retains an immutable proof; preview cleanup preserves later manual edits.
Evidence is in `evidence/agent-planning-visual-state-2026-10-06/`. Other document
proofs, detail crops, full-tool repair and real-model/native UI acceptance remain
open, so the combined visual evidence/repair requirement stays unchecked. All
20 selected final controller/native checks pass and tracked build inputs and
outputs remain unchanged after verification. The running user app was not
restarted; native UI and unrestricted local-model designs remain unqualified.

The first coverage inventory contains 1,756 native Q_INVOKABLE methods and
writable Qt properties across all native domains, including Scene. It records
source hashes and leaves resource/capability/async/reversal contracts explicitly
unclassified. This is an inventory foundation, not automatic registration or
proof of every QML-only workflow.

## Work order

1. Durable task journal and resource adapters, starting with the existing photo
   composition path. Test admission, cancellation, exact revert, conflict
   preservation, interrupted-operation recovery, and write failures.
2. Shared app command registry and complete coverage inventory. Extend adapters
   across documents, catalog operations, settings, and output creation.
3. Long-running planning, checkpoint recovery, fresh proof, and full-tool repair.
4. Native UI wiring and complete real-model acceptance workflows.

The goal remains incomplete until every requirement above has authoritative
current evidence.
