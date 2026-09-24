# Concurrency and lifecycle contract

This document is the canonical ownership record for desktop asynchronous work.
Controllers and models are `QObject`s created on the application thread and may
only be read or mutated on that thread. Worker functions receive immutable value
requests, shared cancellation tokens, and non-`QObject` services. Results return
through queued watcher signals or `QMetaObject::invokeMethod` calls guarded by a
`QPointer` and, where state can change, a request generation.

## Current owner inventory

| Surface | Owning thread | Worker boundary | Terminal/cancellation owner |
| --- | --- | --- | --- |
| App bootstrap | Application thread | One value-only catalog bootstrap task | Application-owned watcher |
| Catalog model | Application thread | Thumbnail render requests and catalog deletion requests | Model-owned watcher pool; per-thumbnail and deletion cancellation tokens |
| Editor preview | Application thread | Render, mask, inpaint, and selection value requests | Controller/mask coordinator watchers and generation checks |
| Timeline document | Application thread | Scope, integrity, diarization, reframe, and media tool tasks | Controller watchers/tokens and controller-owned `QProcess` instances |
| Carousel document | Application thread | Cutout value request | Controller-owned watcher and process helpers |
| Job runner | Application thread | Value-only job requests | Runner-owned watcher and persistent job terminal state |
| Agent edit | Application thread | Planning, proof, materialization, and observation value requests | Controller-owned watchers, run IDs, and cancellation callbacks |
| Histogram/thumbnail coordinators | Application thread | Pure computation requests | Coordinator-owned watcher and generation/cancellation state |

The catalog deletion boundary snapshots helper program, full argument lists,
working directory, process environment, and target IDs before dispatch. Its
worker does not dereference the model. Destruction sets the shared cancellation
token; any in-flight helper is terminated and killed on bounded deadlines.

Timeline shutdown sends cancellation first, kills every owned media process
concurrently, and shares one 1000 ms wait budget across the process set. The
program-scope worker captures only source and revision values, so destruction
disconnects and destroys its watcher without an unbounded `waitForFinished`.
`bounded_process_wait.h` is the single owner of normal bounded-process completion
and shared-budget process shutdown; architecture validation rejects local copies.

Catalog helper launch policy is likewise centralized. `AppBootstrap::runCatalogHelper`
owns helper resolution, the catalog-authority environment, start/finish deadlines,
terminate/kill escalation, merged output, and privacy-safe lifecycle logging. The
job store, timeline asset store, and carousel document store delegate to this
boundary instead of maintaining subtly different `QProcess` loops. Architecture
validation rejects those owners if they reacquire process-environment policy.

## Remaining event-loop debt

The production tree currently contains 15 explicit event-loop pumps: seven in
`agent_edit_controller.cpp`, three in `agent_local_model.cpp`, four in
`agent_mcp_client.cpp`, and one thumbnail-drain compatibility method in
`catalog_asset_model.cpp`. They are legacy synchronous adapters, not approved
patterns for new work. `scripts/test-architecture-boundaries.py` fails if a new
file introduces one or if any current file exceeds its frozen count. These calls
must be replaced with explicit asynchronous states before the phase-1 exit gate.

Any newly introduced async operation must document request identity, owner,
cancellation signal, stale-result rule, and exactly one terminal outcome in the
same change. Tests that merely pump events do not demonstrate production-safe
reentrancy.
