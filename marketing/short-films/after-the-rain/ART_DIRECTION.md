# After the Rain — painted keyframe study

Current brief: a normal cinematic shot, realistically proportioned adults and a
believable machine, with an oil-painted surface informed by Monet's light and
color and visible directional brushwork. The previous SVG/cutout film is not the
visual basis for this work.

The first study uses an eye-level full shot in a wet park. An adult woman offers
a porcelain cup to a small industrial companion robot. Its utility gripper holds
out a wildflower. Their hands, cup, lens and flower occupy the central interval;
the figures have shared footing, cast shadows and a consistent horizon. This
exchange is a blocking study, not an approved complete commercial screenplay.

The painting is authored as pressure-sampled native brush commands, sent through
`photo.edit_composition` to the running Looksy Release app. The Python script
plans mark coordinates, color and pressure; it does not rasterize the painting.
No SVG, generated image, imported reference picture or external painting renderer
is used. Broad ground strokes, dry-ink scumbling and narrow definition strokes
are retained as ordinary native pixel-paint layers and an original brush score.

The nine technical paint layers organize depth and finishing in the document.
They are not a proposal to animate sliding cards. A later moving shot must change
the actual drawings, occlusion and perspective with the camera and performance.

The dedicated session is local to `marketing/outputs/painterly-keyframe/` and is
separate from the existing trailer. The exact board/asset and layer identities,
completion state and brush-call count are recorded in its receipts. The bridge
rejects excess calls before admission; the client paces requests and retries
only that known HTTP 429 rejection. It does not automatically repeat a mutation
after an unknown transport failure.

This is an art-direction study requiring inspection of the actual native export.
Machine checks and stroke counts do not establish artistic quality. No animated
film or completed frame-by-frame native workflow is claimed by this still.

The fallback exporter is `native_canvas_export_driver`, built with the desktop
test-runtime targets. It reads the saved native document and renders its actual
adjustment patch through `render_flattened_output_file`. It writes a new 16-bit
sRGB PNG atomically, refuses an existing destination, and does not change the
document or source. It supports one photo board without board-level overlays.
It is a native renderer utility, not proof of completing the interactive export
dialog or reopening the project in a foreground app.

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/native_canvas_export_driver saved-document.json new-output.png
```

Inspection of the native preview: the composition reads clearly, but the
portrait and hands remain illustrative and the repeated dabs are too uniform.
This study does not yet establish the requested realistically painted film look.
Do not multiply it into an animation before improving the art direction.
