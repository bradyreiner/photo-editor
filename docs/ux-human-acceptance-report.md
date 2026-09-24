# UX Human Acceptance Report

Use this report only for the human-participant gates that cannot be replaced by automated tests. Attach the completed report to `user-experience-improvement-checklist.md` before changing Items 1, 2, or 4 from `[~]` to `[x]` or closing the overall gate.

## Goal

Confirm that photographers can understand and complete the real product path without developer help:

`first launch -> import -> cull -> batch edit -> refine -> compare -> export -> relaunch/recover`

## Test Setup

- App build or commit:
- Test date:
- Facilitator:
- Clean profile location:
- Media set location:
- Media count and formats:
- Duplicate count:
- Unsupported-file count:
- Referenced/offline-media setup:
- Export destination:
- Screen size and scaling:
- Input method: pointer / keyboard-only

## Item 1 — Five First-Time Users

Each participant must import their own photos or open the sample project, rate one photo, make one visible edit, and produce a real exported file. Do not explain implementation terms or give procedural help.

| Participant | Path | Completed | Time | Asked for help | Setup terminology blocked them | Export opened/revealed | Notes |
| --- | --- | --- | ---: | --- | --- | --- | --- |
| 1 |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |
| 4 |  |  |  |  |  |  |  |
| 5 |  |  |  |  |  |  |  |

Gate:

- [ ] All five participants reached a real export without developer help.
- [ ] At least four completed the path in 10 minutes or less.
- [ ] Nobody was blocked by setup or implementation terminology.
- [ ] Skip and restart behavior were tested at least once.

## Item 2 — Human 100-Photo Keyboard Cull

Use a real 100-photo subset. The tester must navigate, pick/reject, rate, change a filter, enter compare or survey, undo one decision, and finish without using the pointer.

- Tester:
- Start and finish time:
- Photos reviewed:
- Keepers:
- Rejects:
- Pointer used: yes / no
- Lost or ignored keystrokes:
- Incorrect active-photo changes:
- Stale rating or filter state:
- Noticeable stalls:
- Remaining friction:

Gate:

- [ ] All 100 photos were reviewed without the pointer.
- [ ] No lost decisions, incorrect active-photo changes, or stale state occurred.
- [ ] The tester could always tell which photo and culling decision were active.

## Item 4 — Five-Second Studio Recognition

Show each image without explanation for five seconds. Ask: “What tool is selected?”, “What will the next adjustment affect?”, and “What is the next primary action?”

- [Clean Adjust — 980 x 760](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/studio-hierarchy-clean-adjust-980x760.png)
- [Pro Crop — 1180 x 820](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/studio-hierarchy-pro-crop-1180x820.png)
- [Inspect Masks — 1600 x 1000](/Users/bradyreiner/.codex/visualizations/2026/07/14/019f624a-dd6a-7291-a8e9-a62170ae5322/studio-hierarchy-inspect-masks-1600x1000.png)

| Participant | Clean Adjust: 3/3 | Pro Crop: 3/3 | Inspect Masks: 3/3 | Confusing or competing element |
| --- | --- | --- | --- | --- |
| 1 |  |  |  |  |
| 2 |  |  |  |  |
| 3 |  |  |  |  |
| 4 |  |  |  |  |
| 5 |  |  |  |  |

Gate:

- [ ] Every participant identified the selected tool, target, and next primary action in all three images.
- [ ] No participant reported a duplicated or competing primary action.

## Final Representative Workflow

- [ ] Start with a clean profile and complete first-run guidance with a real import.
- [ ] Import at least 500 mixed RAW/JPEG assets, including duplicates and one unsupported file.
- [ ] Cull to at least 50 keepers using the keyboard.
- [ ] Apply a selective batch edit to at least 20 photos.
- [ ] Refine one photo with a layer and local selection.
- [ ] Create a snapshot and verify undo, redo, and history.
- [ ] Compare original, edited, and snapshot states at a zoomed position.
- [ ] Move or disconnect referenced media, relaunch, and bulk relink it.
- [ ] Export at least 20 photos, cause one failure, and retry only that failed work.
- [ ] Force quit during a separate safe operation and verify recovery.
- [ ] Repeat the core path at maximum UI scale with keyboard-only navigation.
- [ ] Attach preview/export parity evidence and the final delivered files.

## Outcome

- Total completion time:
- Errors encountered:
- Recoveries attempted and result:
- Blocking issues:
- Remaining friction:
- Evidence links:
- Reviewer:
- Review date:
- Final result: pass / fail

