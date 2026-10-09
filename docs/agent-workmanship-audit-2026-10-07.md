# Agent workmanship audit — October 7, 2026

The target is reliable completion of supported creative work: understand the
brief, discover the right tools, construct an editable result, inspect rendered
pixels, repair defects, save/reopen, and deliver the requested output. A stronger
prompt is one part of that target. It does not supply missing native resource
owners or establish professional taste.

## Current tool knowledge

The fresh [inventory](evidence/agent-workmanship-2026-10-07/native-tool-inventory.json)
records 50 existing MCP tools and 1,810 Qt invokable methods or writable
properties. These counts include internal and administrative surfaces; they are
not counts of safe, enabled editing operations. The photo adapter explicitly
classifies 14 reads, 75 methods and 49 writable properties, with further argument,
permission and resource checks at dispatch. Timeline has its first native adapter.
Production main still registers Library, Carousel, Scene, Export, preferences and
jobs with an unavailable ownership classifier. Blank canvas and photo output
have separate task actions and must be assessed through their actual receipts.

The model receives schemas at each turn and can discover native commands. It
must not memorize every reflected method or assume every listed method is
available. Full knowledge means selecting the relevant domain, paging discovery,
reading the current schema, understanding its units and dependencies, using
returned IDs, and interpreting completion and failure receipts correctly.

## Changes in this pass

`agent_workmanship.h` provides the same compact workmanship contract to the
conversation planner, construction planner and rendered evaluator. It describes
visual hierarchy, spacing, readable copy, photographic restraint, purposeful
discovery, revisions of existing layers, rendered repair and evidence for each
stage of delivery. It does not grant tool permissions or claim taste calibration.

Two conflicting construction instructions were removed. Text and poster work
previously simultaneously required composition tools and a recipe-patch path;
layers also unconditionally required a patch schema even when the native tool
schema was the actual route. The planner now selects the supplied typed
composition/native path and requests patch schemas only for a real patch tool.

The local-model regression test checks the actual outbound prompts at all three
stages and rejects those conflicting legacy instructions.

The live sample also exposed an inaccurate tool schema: operations that edit an
existing layer advertised `layerId` as optional. A shared composition contract
now marks the 16 target-dependent operations in discovery and rejects missing,
empty or whitespace-only IDs before dispatch, with an explicit correctable,
unapplied argument receipt. Creation, selection and copying from the base image
retain their valid target-free routes. The native registry contract tests the
real discovery schema and missing/empty-ID refusal before a document handler is
called; the reference sample exercises successful edits with returned IDs.
Operation predicates also now allow the other argument fields in a real call.
Previously their closed object shape meant a standards-compliant JSON Schema
validator could never match the intended operation condition.

The next unrestricted attempt used the correct IDs but combined typography and
position fields, producing a generic native no-change failure. Typography now
uses the native registry's strict partial-update schema in both discovery and
pre-dispatch validation. Invalid coordinates, empty updates, string weights and
out-of-range sizes return a certified unapplied, recoverable argument error;
position belongs to the transform operation. Regression checks verify that none
of these invalid updates reaches the document handler.

An opt-in native design sample driver and Python runner now provide two explicit
modes using the same authenticated MCP bridge, discovered tool schemas, task
journal and renderer:

- `reference`: a host-authored example, with every actual tool call and returned
  layer ID retained. It demonstrates native capability, not model autonomy.
- `model`: the installed local vision model chooses every construction call,
  with no phase-specific operation selection or ID injection. Each turn receives
  a fresh hashed rendering and actual tool receipts; failed calls withhold later
  dependent calls. Its evaluation budget is 24 turns, separate from production's
  productive-continuation behavior.

Both save the source, editable recipe, actual pixels and detailed receipts.
Verification checks exact copy and layer count, text bounds, every reopened
export pixel, source preservation and exact whole-task reversal. Model success
also requires the model to finish construction and a passing rendered critique
without unresolved review. Full-resolution text crops accompany the critique.
Rejected completion candidates return specific rendered and layout findings to the same model/tool loop for repair. This driver evaluates construction, repair and final review; it does not replace the
production conversation, production repair orchestration, catalog saves or application UI. The final isolated run uses a disclosed frozen controller baseline because other work is building in the shared checkout; it does not qualify those concurrent controller changes.

## Completion standards to qualify

| Workflow | Required finished behavior | Current acceptance status |
|---|---|---|
| Existing-photo editorial cover | Exact copy, type hierarchy, protected photograph, editable layers and export | Reference passes; unrestricted model stops on repeated invalid styling calls and fails hierarchy |
| Thumbnail behind a subject | Real selection/cutout, required layer order, legible headline, one revision | Earlier unrestricted native app attempt failed; guided evidence is insufficient |
| Blank poster | New owned canvas, complete editable typography/shapes, correct dimensions, save/reopen/export | Creation owners exist; complete model/native acceptance remains open |
| Product ad | Protected product/color, supplied factual copy, purposeful layout and export | Requires a complete acceptance run |
| Wedding batch | Representative sample approval, consistent edits, outlier review and per-asset durable saves | Requires current model and photographic qualification |
| Video sequence | Real enrolled timeline operations, decoded-frame review, save/reopen and playable output | Timeline adapter coverage is partial |

For every workflow, also test a specific revision, cancellation during native
work, exact whole-task reversal, and preservation of later unrelated manual
edits. Reject missing copy, rejected visual critique, unresolved async work,
failed save/export receipts and unowned resources. Preserve partial attempts for
diagnosis; never count a reference or injected fixture as autonomous acceptance.

## Remaining work in priority order

1. Resolve the current model's repeated invalid-call loop. The unrestricted
   final evaluation received real IDs and explicit unapplied argument errors
   but repeated typography calls containing x/y. Operation-specific generation
   constraints and the repair continuation need qualification with this exact
   failure case. A stronger prompt alone did not solve it.
2. Qualify the complete existing-photo workflow through the production session
   and repair loop. Resolve the observed stale catalog versus live composition
   handoff and require the approved visual relationship in the final output.
3. Give every remaining native domain a resource owner, capability checks,
   asynchronous receipts, cancellation and exact conflict-aware reversal.
4. Expand proof and repair to each document type and each requested deliverable.
   A photo proof cannot establish timeline playback or a carousel's other boards.
5. Qualify checkpoint recovery and relaunch without repeating accepted mutations.
6. Use independent human review across varied real images and design briefs to
   calibrate taste. Model self-review is useful evidence, not a human rating.

Detailed results and reproduction commands are in the
[evidence record](evidence/agent-workmanship-2026-10-07/README.md).
