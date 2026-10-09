# Simple conversational agent and retro terminal experience

Implementation plan, October 6, 2026. This records the agreed product direction
and proposed work. Unchecked items are not shipped or qualified behavior.

## Product outcome

The user opens the optional agent from their current workspace, types what they
want, answers a few relevant questions, reviews a short plan and presses Start.
The agent completes the work, checks the result and offers an editable draft.
The detailed planning, tool selection and recovery machinery stay behind this
simple interaction.

## Simple on the user side

The broader planner is an internal capability, not another interface the user
has to learn. Every task uses the same five screens:

1. **Ask:** "What would you like to do?" Show a small current-target indicator,
   a prompt field and optional Use inspiration. Changing the target is available
   without making scope selection a required first step.
2. **Clarify:** Ask only for missing information that affects the result, one
   or two questions at a time. Skip this screen when the prompt is complete.
3. **Approve:** Present a few plain-English outcome bullets and Start. Allow
   changes to the brief. Keep tool names and execution dependencies internal.
4. **Work:** Show the actual current activity, progress when measurable and
   purposeful character animation. Keep Close, Stop and Revert available.
5. **Review:** Show the editable result and "What would you like to change?"
   Keep delivery/export choices relevant to the requested outcome.

For example, "Make a YouTube thumbnail from this photo" might need a question
about the title and intended mood. Its plan could say "Frame the photo for
YouTube, brighten the person, and place the title behind them." Choosing
segmentation tools, masks, layers and repair steps is the agent's job.

Do not add mandatory task categories, a workflow builder, an exposed dependency
graph or repeated approvals of individual tool calls. Ask again when essential
information is missing or a material change to the approved result is needed.

## Delivery checkpoints

The six implementation phases below have these user-visible checkpoints:

| Order | Deliverable | Completion evidence |
| --- | --- | --- |
| 1 | One prompt-to-plan conversation for any supported task | Relevant questions, retained context and no edits before Start |
| 2 | One complete existing-photo thumbnail workflow | Actual installed model produces editable text behind the person, revises it, and supports Stop/Revert |
| 3 | A polished retro terminal and character experience | Native screens and motion recordings show readable pages, real control contact and usable controls at all supported sizes |
| 4 | Blank-canvas posters, wedding batches and ads | Same simple conversation; correct editable results and reversible document/file creation |
| 5 | Broader app tools and restart recovery | Owned operations across workspaces; resume without duplicate edits and preserve later manual changes |
| 6 | Full end-to-end qualification | Real app/model workflows, save/reopen/export and interruption checks, with recorded evidence |

Expand task coverage after the first workflow works end to end, rather than
adding more user-facing setup. Existing components and passing fixtures are
foundations; they do not make an unfinished checkpoint complete. The numbered
phases group the work; motion in phase 3 is deferred while functional work in
phases 2, 4, 5 and 6 proceeds.

Current functional priority: finish tool execution, rendered-result repair,
save/reopen, revision and Stop/Revert in the actual app with the installed model.

October 7 visual correction: the generated raster character and illustrated
loading scenes were rejected. The shared components now use a compact drawn
line character with one body, small eye marks, restrained CRT color and an
integrated activity filament. Startup, Library, Timeline, import/export and
activity indicators reuse that same form. Native style and motion captures are
available in [the character evidence](evidence/line-character-system-2026-10-07/README.md).
This visual foundation does not close the independent model-execution,
control-contact or full-app workflow acceptance gates.

The first complete workflow is an existing photo to a YouTube thumbnail. Blank
canvas posters, wedding batches and ads follow using the same conversation and
execution system. These acceptance workflows do not narrow the full app access
objective in [agent-full-app-execution-plan.md](agent-full-app-execution-plan.md).

## User experience requirements

- Entry shows one prompt, a compact target indicator and Use inspiration.
  Context comes from the open photo, selected assets or project. A blank canvas
  is a valid starting context, including when no photos are available.
- Use inspiration opens saved looks and past reference photos. It is optional;
  ordinary requests do not require the user to create or choose a style first.
  Reference assets remain distinct from assets to edit.
- Ask one or two questions per screen. Reuse information already supplied in
  the prompt, project or inspiration. Suggest editable defaults for ordinary
  design choices; ask for essential facts such as unknown event dates or exact
  headline copy instead of inventing them.
- The user never has to configure tool chains, layer IDs, model parameters or
  an internal task category. They can change an answer or target without
  restarting the entire conversation.
- Show a short plan describing the result, selected targets, exact important
  copy and deliverables. Start approves those outcomes and begins execution.
  The agent can adjust internal tool steps within that brief; a material change
  to the goal, scope or deliverables returns to the user.
- The main terminal has no vertical scrolling. Replace focused pages; paginate
  inspiration and optional history. Keep advanced details separate from the
  primary conversation and usable without shrinking text.
- During work show meaningful stage messages and real progress where available.
  Keep Stop and Revert accessible, show their actual status, and provide Close
  or Minimize even during errors. Closing the surface must have an explicit,
  consistent relationship to background work; it must not silently cancel it.
- Present an editable result with a simple revision prompt. Do not overwrite
  original sources as part of ordinary draft delivery. Export uses the agreed
  destination and output contract.

The visible sequence is:

`Prompt -> Relevant questions -> Short plan / Start -> Working -> Result / Revise`

Questions can be skipped when the brief is already complete. Use inspiration
returns to the same conversation. Revision retains the existing composition
and task baseline. Interrupted work retains its brief and explains the next
available action.

## What the user actually sees

The experience should feel like giving a creative assistant a brief. The same
prompt works whether the context is one photo, a selection, a project or a blank
canvas. The planner chooses the relevant tools internally.

| Request | Ask only if missing | Short plan example |
| --- | --- | --- |
| Make a YouTube thumbnail from this photo | Exact title and desired mood | Frame for YouTube, improve the photo, place the title behind the person |
| Edit these wedding photos | Desired look and delivery needs | Apply a consistent look, adjust each photo, flag images needing attention |
| Create a poster for an event | Event facts, exact copy and intended size | Create the canvas, arrange the supplied details, finish an editable poster |
| Make an ad using this project | Message, destination and required brand assets | Use the project assets, build the layout, prepare the agreed output |

These are examples, not task-category buttons or mandatory questionnaires.
Known answers are reused. Ordinary aesthetic choices can have clearly stated,
editable defaults. Missing event facts and required wording need an answer.

For the first thumbnail milestone, the approval page should be as simple as:

> **Selected:** 1 photo  
> **Title:** the wording supplied by the user  
> **Plan:** Frame for YouTube; improve color and contrast; place editable title
> text behind the person.  
> **Start** / **Change brief**

While it works, show real messages such as "Selecting the person" or "Checking
the title placement." The character can travel to the relevant controls and
use a temporary tool. Completion opens the editable result and a revision
prompt. The first milestone is accepted only after this whole path works in the
native app with the installed model, including save/reopen, Stop and Revert.

## Visual and character direction

The normal editor retains its sophisticated appearance. The optional agent has
an original retro future terminal identity inspired by 1980s AI and CRT displays.

- Use crisp terminal typography, readable contrast, restrained scanlines,
  subtle RGB color fringing and occasional intentional glitches. Use a richer
  palette than monochrome green. Effects must preserve image and text clarity.
- Refine the approved compact character into a distinctive brand asset. Its
  texture should look rendered on a display, with no canvas grain, riveted metal
  costume or tool belt. Keep emotion understated and motion fluid; no breathing
  loop or exaggerated human presentation.
- Keep the character separate from a permanently carried screen. Let it travel
  to real UI anchors, reach controls and temporarily produce a relevant tool.
  Contact points must follow window geometry and resizing.
- Tie inspection, adjustment, masking, checking, waiting and completion motions
  to structured task events. Animation is presentation of real work; task
  success must come from the actual operation receipt.
- Use occasional concise humor and small reactions. Maintain useful messages
  during failures and long waits. Reduced motion replaces travel/glitches with
  quiet state changes while retaining all text and controls.
- Design the terminal and character as one coherent scene with a distinctive
  outer silhouette, restrained surrounding chrome and no large logo panel.
  An original visual direction and native rendered review are required.

## Current code and evidence

The legacy `AgentWalkthroughDialog.qml::runTerminalDirection()` learns selected
references when needed and emits `draftRequested` directly. Implementation now
adds an injected `AgentSessionController` and `AgentSessionTerminal.qml` for the
prompt/questions/plan/Start/result flow; `StudioWorkspace.qml` supplies actual
workspace targets and production wiring forwards approved snapshots to execution.
Existing legacy drafts retain their review route. Full production execution and
all-workflow qualification remain open.

Existing components include `AgentConversationView.qml`, `LooksyAgentCharacter.qml`
and `AgentWorkspaceOverlay.qml`. They provide a starting point for the terminal,
character and workspace anchors, but source presence does not establish the
requested appearance or fluid interaction.

Existing backend work includes typed native command discovery, the first photo
and Timeline resource owners, productive asynchronous continuation, certified
tool-error correction, queued revisions and fresh rendered photo evidence.
The latest 20 selected checks are recorded in
[planning evidence](evidence/agent-planning-visual-state-2026-10-06/README.md).
Those deterministic/native checks do not qualify unrestricted local-model
creation, all-workspace access or the real UI. Remaining backend gaps are
recorded in [the execution audit](agent-complex-edit-blockers-2026-10-06.md).
Current conversation, schema, canvas-source and native terminal fixture evidence
is recorded in [integration evidence](evidence/agent-conversational-execution-2026-10-06/README.md).
Native fixture screenshots demonstrate the CRT scene and character; injected
model/executor interactions do not replace actual model design acceptance.

## Phase 1: Conversation, brief and approved plan

- [ ] Add a session layer above `AgentEditController`. Keep conversational state
  independent from its existing draft/execution state. Proposed types include
  `AgentTaskBrief`, `AgentTaskPlan` and `AgentSessionController`. The session
  implementation now uses typed QVariantMap contracts rather than separate
  brief/plan classes; final behavior still needs qualification.
- [ ] Capture prompt, target IDs/context, requested output, exact copy, answers,
  inspiration IDs, known project preferences and explicitly chosen defaults.
  The internal brief can describe any supported task; do not make users select
  from a rigid list of thumbnail/poster/wedding modes.
- [ ] Add structured local-model responses for asking questions, presenting a
  plan and explaining unavailable capabilities. Clarification and planning use
  read-only observations and cannot dispatch editing commands.
- [ ] Bind each plan to its brief version and concrete target snapshot. Editing
  a material answer invalidates the old plan; target changes before Start must
  be detected and reflected in the visible target indicator and updated plan.
- [ ] Derive reasonable task-specific defaults and internal permissions. Preserve
  explicit user constraints and existing project preferences without making a
  photographic style profile a prerequisite for a blank design document.
- [ ] Replace the popup's direct draft submission with session submission,
  question pages and plan review. Keep the installed local model usable through
  existing discovery/readiness logic; show setup only for a verified missing or
  unavailable runtime.

Acceptance: a complete prompt goes straight to a short plan; an incomplete
event brief asks only for missing facts. No edit occurs before Start. Answers,
inspiration and targets survive Back, plan changes and retry. Blank-canvas
requests can reach a plan without an existing photo. Invalid model responses
retain the conversation and offer a useful next action.

## Phase 2: Complete thumbnail execution and revision

- [ ] Start consumes the approved brief/plan snapshot. Execution may refine its
  internal steps but must retain the approved targets, copy and deliverables.
  Capture baselines before the first mutation through the existing coordinator.
- [ ] Connect one full existing-photo thumbnail path: framing, color edits,
  subject mask/cutout, editable title, intended layer order and effects.
- [ ] Give native design arguments useful semantic schemas: units, coordinate
  origin, enum values, allowed ranges and supported nested typography/fill fields.
- [ ] Let rendered-result repair use the same typed composition tools as initial
  construction. Preserve completed operations and real returned IDs. Supplement
  visual critique with exact-copy, geometry and layer-order checks, plus detail
  crops when overview resolution cannot establish text or mask quality.
- [ ] Convert blocking subject segmentation and other heavy operations used by
  this workflow into owned asynchronous jobs with cancellation and completion
  receipts. Productive work has no automatic elapsed-time cutoff.
- [ ] Route Start, Stop, Revert, revision and final draft delivery through one
  task owner. Reconcile partial failure; block late callbacks after cancellation
  or restoration; preserve subsequent manual changes.

Acceptance: the selected local model creates a real editable thumbnail with
specified text behind the person, checks and revises it, and saves/reopens the
same composition. Stop works during both generation and native pixel work.
Whole-task Revert restores the original document after success, revision or
partial failure. Current native/controller tests support this check but do not
replace the actual app/model workflow.

## Phase 3: Terminal scene, motion and live feedback

- [ ] Produce a coherent native UI prototype covering prompt, questions, plan,
  working, result and failure. Review the approved character treatment and CRT
  texture in the actual rendered interface before expanding animation assets.
- [ ] Implement the scene in the existing conversation/dialog/character/overlay
  components with shared typography, color and motion rules. Refactor focused
  pages out of the large dialog where useful.
- [ ] Drive character travel, tool contact and activity messages from structured
  events and real UI anchors. Keep motion out of the user's pointer/focus path.
- [ ] Stream model activity and suitable concise output while buffering and
  validating final command objects before mutation. Display factual work status;
  do not invent percentages, actions or model reasoning to fill a wait.
- [ ] Verify Close, Minimize, keyboard navigation, focus return, retry, Stop and
  Revert in every applicable state. Preserve the ordinary editor experience
  when the agent is unused or dismissed.

Acceptance: native screenshots and recordings demonstrate every state, fluid
motion and correct contact alignment at 1366x768, 1440x900 and 1920x1080 with
display scaling. Main screens fit without vertical scrolling, text remains
readable, reduced motion works, and long tasks remain visibly active. A build
or screenshot of an isolated art asset alone cannot satisfy this phase.

## Phase 4: Blank posters, wedding batches and ads

- [ ] Add owned blank-canvas creation: task admission without a photo, output
  document creation, dimensions/background, imported assets and draft delivery.
  Revert must include newly created documents and owned files, not just recipes.
- [ ] Add catalog/output resource owners before admitting creation, import or
  export mutations. Stage outputs and define restoration/conflict behavior for
  existing files. Never enable a writer solely because it is discoverable.
- [ ] Route wedding batches through the same brief and plan, using representative
  samples, consistent edits and focused exception review. Keep source/reference
  roles and per-photo baselines explicit behind the simple UI.
- [ ] Add poster/ad questions for exact copy, output dimensions, available brand
  assets and intended destination only when the prompt has not provided them.
  Expose export destinations as meaningful delivery choices, not technical setup.
- [ ] Keep the same visible five-stage flow across all task types.

Acceptance: create a blank-canvas event poster, edit a varied wedding batch and
create an ad from ordinary prompts. Each remains editable, has correct copy and
exported pixels, supports one revision and exact task reversal, and preserves
unrelated documents and files. No manual tool-by-tool steering during acceptance.

## Phase 5: Full app ownership and durable continuation

- [ ] Complete classification of the command inventory, including QML-only
  workflows. Track typed arguments, resource owners, completion, cancellation
  and reversal for every user-facing function.
- [ ] Extend admission and ownership across Library, Carousel, Timeline, Scene,
  Export, preferences and native jobs. Add ordinary Timeline/document target
  admission and fresh rendered evidence for each relevant resource type.
- [ ] Classify remaining native work deadlines and replace productive-work
  cutoffs with cancellation-aware completion and genuine failure detection.
- [ ] Persist brief, approved plan, accepted/completed command IDs, resource
  checkpoints and task status. Recover after relaunch without replaying accepted
  mutations. Reconcile incomplete/uncertain operations before resuming.
- [ ] Integrate restart recovery and interruptions into the same simple terminal
  flow with clear Resume/Revert or conflict-resolution choices.

Acceptance: each inventory entry has an implemented contract or a concrete
remaining gap. Multi-document/file tasks support cancellation and reversal.
Restart recovery resumes completed checkpoints without duplicate edits; journal
write failure and later manual changes remain recoverable and visible. Full app
access is not complete while domain mutations remain unavailable.

## Phase 6: Final qualification

- [ ] Run focused session, planner, ownership and UI checks using the repository's
  preset builds, then exercise the real native app with temporary test projects.
- [ ] Run unrestricted creation and revision of thumbnail, poster, ad and wedding
  workflows on the selected installed local model. Record model/runtime/build
  provenance, prompts, answers, timings, editable documents and exported pixels.
- [ ] Exercise missing assets, rejected tools, long work, cancellation during each
  heavy job family, partial failure, manual-edit conflicts and restart recovery.
- [ ] Review legibility, hierarchy, copy accuracy, subject occlusion, photographic
  fidelity and consistent batch results. Obtain native visual review of the
  terminal scene and motion; do not equate component tests with design quality.

The experience is complete when ordinary prompts finish through the same simple
flow, all agreed deliverables are correct and editable, Stop/Revert/recovery work,
and both the native presentation and real-model results have current evidence.

## First implementation milestone

Deliver Phase 1 and connect it to the existing thumbnail executor: opening the
agent shows the current targets, a prompt and Use inspiration; the agent asks
relevant questions, presents a short plan and starts only after Start. Validate
the conversation and no-mutation boundary before extending backend operations
or expanding visual assets. Finish Phase 2 as the first functional thumbnail experience, then expand the
functional parts of Phases 4, 5 and 6. Phase 3 motion remains deferred and must
not block editing, revision, delivery, recovery or broader task coverage.

## Implementation sequence and review gates

1. **Settle the simple interaction.** Review the prompt, missing-information
   questions, short approval page, working state and revision prompt as one
   connected flow. Use the thumbnail, wedding and event-poster examples above.
   A complete brief must skip questions; a partial brief must reuse known
   answers. The broader planner must not add setup screens.
2. **Prove the first complete job.** Use the installed model and an actual photo
   to create a thumbnail with exact editable text behind the person. Include
   color edits, masking, layering, one revision, save/reopen, Stop and whole-task
   Revert. Resolve execution and persistence blockers before expanding coverage.
3. **Keep the functional interface usable; defer motion.** Check readable
   activity, resizing, no scrolling, dismissal and failure recovery alongside
   the real edit. Character movement, control contact and visual polish follow
   functional editing and broader task coverage. Future animation must reflect
   actual work.
4. **Expand through the same interaction.** Complete blank-canvas posters,
   wedding batches and project-based ads. Add the document, catalog and output
   ownership each requires; keep the visible conversation unchanged.
5. **Finish durable execution and broader tool coverage.** Complete remaining
   workspace contracts, native-job cancellation and restart recovery. Productive
   work continues until completion or Stop; interrupted operations reconcile
   before resuming, and later manual edits remain protected.
6. **Accept the whole experience.** Demonstrate all four creative workflows with
   real model decisions, editable documents and inspected exports. Exercise long
   work, failure, Stop, Revert and relaunch in temporary projects. Record actual
   outcomes and remaining gaps, not just passing builds or scripted fixtures.

These gates organize implementation; they are not extra steps shown to the user.
Across every milestone the user sees only:

**Describe it → answer what is missing → review a short plan → Start → revise.**
