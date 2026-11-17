# RAW cursor target correction proposal (P6-bound, unexecuted)

Read-only review of P6 document hash `167bf753c47f812c4da8d16ab75607a51a54f4ada94b850e9cf9409ad696b423`. No native timeline, project file, running app, or MCP state was changed. Exact command fragment: `raw-cursor-target-correction-p6.proposal.json`.

## Finding

At global 44.5667, native partial frame 1337 places the cursor about 160 px left of the loupe on the boat image. The matching full-resolution Remotion frame places it on the white-outlined cell. P6 cursor clip `d92d4571-140b-4a80-be91-578d28fa7a46` has local key `(X=.19575272, Y=-.24588431, scale=.62940882)` at 5.5667; the source-derived pose is about `(X=0.768646, Y=-0.101820, scale=0.629409)`. At local6.0, P6 has `X=-.146667, Y=0`, while the source cursor remains over cell (2,3), around `(X=0.763542, Y=0.228889)`. P6 click keys scale to `1.12`; the source cursor compresses to `0.9`.

## Bounded native proposal

One `timeline.action.setTimelineClipAnimation` operation replaces only `frameOffsetX`, `frameOffsetY`, and `frameScale` on the existing vector path. It passes original keys strictly before local5.0 unchanged, keeps the existing local5.0 boundary values, then uses sparse reveal/move/click/close poses. The source sequence opens the loupe, targets columns 2→3→4, moves to row4 for the erase at48.5s, and follows the target as the grid closes. Opacity, path, static frame, cells, counts, badges, text, tracks, and all other clips are outside the edit.

Remotion uses quintic smoothstep; native `ease-in-out` is cubic smoothstep. The target endpoints and click frames are source-derived; native intermediate easing is approximate and root should judge it in the render. These values are a target plan, not render-parity proof.

## Guard and review

The fragment is strictly guarded to P6 and is stale while root authors P7. Do not execute it as-is. After saving P7, root should read the live timeline, bind the same cursor ID and fresh channel hashes to the new document hash, then render the A/B frames. Verify landscape `framingKeyframes` equal the corrected main animation.

Render global 44.0,44.2,44.5667,44.8,45.0,45.9,46.2,46.7,46.8,47.1,47.6,47.7,48.0,48.4667,48.5,48.7,49.0,49.2,49.8. Verify pointer-to-cell alignment and count changes 25→26→27→28→27. Only P6 frame1337 is a native observation; later cursor states are inferred from P6 keys and reference frames.

## Next visual priority after RAW

The existing full-film review identifies 00:22–00:38 as the next candidate: preserve highlight/shadow, color and curve coverage while making each adjustment visibly change the boat image. This is based on one-frame-per-second P3 reference samples, not P7 native-render evidence.
