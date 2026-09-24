# Checklist Ownership Index

`docs/checklist-index.json` is the canonical ownership registry for every top-level
roadmap, plan, audit, backlog, strategy, and checklist. CI rejects unindexed files,
missing owners, multiple path/ID claims, non-active canonical owners, and duplicate
`<!-- work-id: AREA-NNN -->` markers.

Use the active owner below to change status or add work. Supporting documents may
hold specifications and evidence; historical, parked, and completed documents must
link to their active owner and must not receive independent implementation status.

| Scope | Active owner |
| --- | --- |
| Engineering quality and release readiness | [engineering-a-rating-remediation-checklist.md](engineering-a-rating-remediation-checklist.md) |
| Capability acceptance | [capability-verification-plan.md](capability-verification-plan.md) |
| Photographer workflow and UI | [user-experience-improvement-checklist.md](user-experience-improvement-checklist.md) |
| Color engine and render foundations | [color-engine-and-app-foundations-completion-strategy.md](color-engine-and-app-foundations-completion-strategy.md) |
| Performance evidence | [performance-measurement-expansion-checklist.md](performance-measurement-expansion-checklist.md) |
| Masking and layers | [mask-layer-selection-checklist.md](mask-layer-selection-checklist.md) |
| Local agent experience | [local-model-agent-ux-checklist.md](local-model-agent-ux-checklist.md) |
| Timeline release scope | [timeline-mvp-checklist.md](timeline-mvp-checklist.md) |
| Carousel release scope | [carousel-delivery-100-checklist.md](carousel-delivery-100-checklist.md) |
| One-time production launch | [production-launch-checklist.md](production-launch-checklist.md) |
| Per-version release procedure | [release-checklist.md](release-checklist.md) |

Before adding a new tracker, extend the active owner or register the new document
with a non-overlapping scope. Before implementing an item repeated across documents,
place one stable `work-id` marker in the canonical owner and replace other copies
with links to that marker.
