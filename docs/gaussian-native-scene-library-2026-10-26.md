# Native editable scene library

The four dense film studies are installed as self-contained editable projects
under `.photo-editor/scenes/film-lab-2026-10-26/`. This local runtime directory
survives deleting or rebuilding `build/`. It is intentionally ignored by Git.
The capture credits and source licenses are retained in `SOURCE-NOTES.md`.

Launch `./scripts/run-native.sh`, open **Scene**, then **Saved scenes**:

- Portrait through foreground foliage
- Dense office interior
- Motorcycle in forest
- Portrait with bokeh lights

Each menu entry opens the ordinary native project and recalls its authored
Explore view at 32 pupil samples. High-quality study shots and existing camera
paths are preserved in Shots & camera path. Adjust the camera and film in the
inspector. Use Scene composition to add lights, objects and meshes. Composition
edits persist in the project; Save view stores a camera/film recipe as a shot.
The bokeh project includes three editable emissive spheres.

`run-native.sh` exports `PHOTO_EDITOR_SCENE_LIBRARY_DIR`, defaulting to
`.photo-editor/scenes` in this checkout. That directory's `index.json` lists
relative project paths, display names and optional saved-view IDs. A custom
library can be selected with the same environment variable. Missing projects
are omitted from the menu; opening still uses the existing integrity validator.
The library changes neither the active project nor its settings until chosen.

The normal runner defaults to the optimized Release build. Existing running
instances retain their loaded UI until restarted. Verification builds use
`./scripts/run-native.sh --release --build-only`, which does not stop an editor
or open the user's catalog.
