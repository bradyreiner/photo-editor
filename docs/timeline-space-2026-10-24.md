# Timeline layout and command density

The Timeline now prioritizes Program and the sequence instead of stacking rows
of panel selectors, Source operations, and named tools above them.

- One header owns the timeline name, history, Import, task layout, Panels, Render,
  Save, and Project. Task selection also exposes Reset current layout.
- The Panels menu exposes all eleven panels with open-state checkmarks. Choosing
  a panel focuses it without closing others in the current layout.
- Source operations remain in Source, Actions, and their keyboard commands. The
  duplicate workspace-wide Source command strip is removed.
- Editing tools use square icon buttons, accessible names, active-state fills,
  concrete disabled reasons, and tooltips with configured shortcuts. Creation
  belongs in Actions; Title and Shape remain available there. The vertical tool
  palette retains labels and creation controls.
- Tools and sequence commands share one wrapping toolbar. Link/Snap/Ripple use
  active icon states. Fit labels appear when space permits. Marker review stays
  contextual. Tracks no longer reserve height for a separate palette row.
- Factory Editing uses one Media dock and one tabbed Effects/Inspector dock.
  Color shows Color and Scopes; Audio prioritizes Media and Mixer; Finish shows
  Media, Inspector, and Captions. Extra panels remain available from Panels.
- Switching tasks restores that task's saved panel visibility and sizes. It no
  longer unions incoming visibility with every outgoing open panel. Explicitly
  saved custom layouts remain intact. Use the task menu's Reset current layout
  action to adopt the new factory arrangement in an existing workspace.
- Secondary side docks merge into tabs below 1900 logical pixels, keeping Program
  wider. A requested Color panel survives rapid tab changes while loading.
- Menu-button tooltips disappear while their menu is open.

## Validation

The release application and updated proof driver built successfully. Five
focused Timeline tests passed: tool palette, workspace layout state, panel dock
state, panel dock UI, and source consistency. Popup runtime checks passed.
Command style passed all configurations after building its initially missing
executable. The source style audit passed across 292 QML files; `git diff --check`
was clean. Logs are in [checks](evidence/timeline-space-2026-10-24/checks).

The final native Cocoa workspace run uses an isolated catalog and generated
video/image fixtures. Its ten captures and receipt are in
[native-final](evidence/timeline-space-2026-10-24/native-final). It checks the header
menus with pointer input, all eleven menu-row bounds, Source focus, layout reset,
independent saved sizes, splitter dragging, selection/playhead preservation,
rapid Color tab switching, Color input controls, Audio/Finish layouts, laptop
portrait/landscape geometry, and detached-panel restoration after view recreation.

Reviewed examples:

- [Editing](evidence/timeline-space-2026-10-24/native-final/timeline-ux-editing-wide.png)
- [Laptop Color](evidence/timeline-space-2026-10-24/native-final/timeline-ux-color-landscape-laptop.png)

The diagnostic native run completed all interaction assertions, but its overall
receipt intentionally remains `passed: false`: cold Timeline/grid readiness was
6447 ms against the existing 1500 ms budget. First Color was 560 ms against
1000 ms; warm Color/Editing p95 were 116/140 ms against 250 ms. No budget was
relaxed. The initial source-QML cold run and intermediate failures remain in the
evidence directory. Final logs also retain a separate active-binding warning in
TimelineEffectsPanel during recreation; the Color Loader warning found during
this pass was resolved.

The extended native editing-tools proof stopped at its generated-video
media-analysis readiness gate before reaching the reorganized UI. It does not
qualify the advanced editing tools in this pass. Its log is retained as
[direct-tools.log](evidence/timeline-space-2026-10-24/direct-tools.log).

This is scoped layout and interaction evidence from the current dirty checkout,
not full video-editor or release qualification. Unrelated workspace edits were
preserved. Source, executable, and capture hashes are recorded in
[verification.json](evidence/timeline-space-2026-10-24/verification.json).
