import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    objectName: page === "library" ? "libraryStudioWorkspace"
                : page === "edit" ? "editStudioWorkspace"
                : "exportStudioWorkspace"
    color: "transparent"
    focus: true

    Theme { id: theme }

    property var catalogAssets: null
    property string page: "library"
    property int selectedPresetIndex: 0
    property int selectedCompareIndex: 1
    property int selectedMaskSetIndex: 0
    property int activePhotoLayerIndex: 0
    property bool selectionMode: false
    property bool importModeOpen: false
    property bool syncEnabled: true
    property bool decontaminateEnabled: true
    property bool librarySourcesPanelVisible: true
    property bool libraryInspectorPanelVisible: true
    property bool editProjectsExpanded: true
    property bool editCollectionsExpanded: true
    property bool editUtilitiesExpanded: true
    property bool editSidebarCollapsed: false
    property bool editContextPanelVisible: true
    property bool editInspectorPanelVisible: true
    property bool exportPresetsPanelVisible: true
    property bool exportSettingsPanelVisible: true
    property string interfaceMode: "pro"
    property bool fullScreenEditingEnabled: false
    property bool responsiveSmallDisplayPanelsActive: false
    property bool lightAdvancedToneExpanded: false
    property bool lightLevelsExpanded: false
    property bool lightZonesExpanded: false
    property bool cropTransformExpanded: false
    property bool cropLiquifyExpanded: false
    property bool colorMixExpanded: true
    property bool channelMixerExpanded: false
    property bool blackWhiteExpanded: false
    property bool colorReplaceExpanded: false
    property bool skinToneExpanded: false
    property bool harmonyExpanded: false
    property bool splitToningExpanded: false
    property var savedWorkspaceLayouts: [
        {
            name: "Default",
            librarySourcesPanelVisible: true,
            libraryInspectorPanelVisible: true,
            editSidebarCollapsed: false,
            editContextPanelVisible: true,
            editInspectorPanelVisible: true,
            exportPresetsPanelVisible: true,
            exportSettingsPanelVisible: true
        },
        {
            name: "Focus",
            librarySourcesPanelVisible: false,
            libraryInspectorPanelVisible: false,
            editSidebarCollapsed: false,
            editContextPanelVisible: false,
            editInspectorPanelVisible: false,
            exportPresetsPanelVisible: false,
            exportSettingsPanelVisible: false
        }
    ]
    readonly property int savedWorkspaceLayoutCount: savedWorkspaceLayouts.length
    property bool scopeShowRgb: true
    property bool scopeShowLuma: true
    property bool scopeShowDensity: true
    property bool scopeShowTrace: true
    property bool scopeShowGrid: true
    property bool scopeLogHistogram: false
    property bool largeScopesMode: false
    property int selectedToneWheel: 1
    property string cropGuideMode: "Thirds"
    property real zoomValue: 1.0
    property string viewMode: "fit"
    property string catalogViewMode: "grid"
    property int surveyAssetCount: 0
    property string compareViewMode: "split"
    property real compareSplitPosition: 0.5
    property real synchronizedZoom: 1.0
    property real synchronizedPanX: 0
    property real synchronizedPanY: 0
    property int compareViewportCount: 0
    property bool loupeEnabled: false
    property real loupeX: 0
    property real loupeY: 0
    property real loupeZoom: 2.0
    property bool focusCheckOverlayEnabled: false
    property bool focusPeakingOverlayEnabled: false
    property bool clippingOverlayEnabled: false
    property bool falseColorOverlayEnabled: false
    property bool gamutOverlayEnabled: false
    property bool zoneMapOverlayEnabled: false
    property string scopeLayoutPreset: "Stacked"
    property string scopeSamplingMode: "Balanced"
    property string editTool: "adjust"
    property string cropToolMode: "crop"
    property string maskCreationOperation: "add"
    property string editInspectorTab: "adjust"
    property string selectedEditLayer: "adjustments"
    property string exportDestination: "/Volumes/Exports"
    property string exportNameSuffix: "_virel"
    property string searchText: ""

    FileDialog {
        id: referenceImageDialog
        title: "Choose Reference Image"
        nameFilters: ["Images (*.jpg *.jpeg *.png *.tif *.tiff *.heic *.heif *.webp *.dng *.raw)", "All files (*)"]
        onAccepted: {
            if (editorPreview.setReferenceImage(selectedFile))
                root.activateEditTool("compare")
        }
    }

    onEditToolChanged: {
        if (editTool === "compare") {
            editorPreview.compareMode = true
            editorPreview.cropEditing = false
            return
        }
        if (editTool === "crop") {
            editorPreview.compareMode = false
            editorPreview.cropEditing = root.cropToolMode === "crop"
            return
        }
        editorPreview.compareMode = false
        if (editTool !== "crop")
            editorPreview.cropEditing = false
    }

    signal assetActivated(string destination)
    signal projectActivated(string destination, string projectName)
    signal localAssetActivated(string filePath, string previewSource, string displayName)

    readonly property color panelColor: "#0D1113"
    readonly property color panelLift: "#11171A"
    readonly property color lineColor: "#22292D"
    readonly property color lineBright: "#314045"
    readonly property color hairline: theme.alpha(lineColor, 0.58)
    readonly property color hairlineStrong: theme.alpha(lineBright, 0.78)
    readonly property color mint: "#19E0B0"
    readonly property color mintSoft: "#44F3CC"
    readonly property color dimText: "#79828C"
    readonly property color faintText: "#4E5862"
    readonly property bool compactMode: interfaceMode === "compact"
    readonly property bool editingChromeVisible: !fullScreenEditingEnabled
    readonly property bool smallDisplayMode: width > 0 && width < 1120
    readonly property int librarySourcesPanelWidth: compactMode ? 218 : 262
    readonly property int libraryInspectorPanelWidth: compactMode ? 292 : 340
    readonly property int editContextPanelWidth: compactMode ? 210 : 246
    readonly property int editInspectorPanelWidth: compactMode ? 356 : 420
    readonly property bool editContextPanelActive: editingChromeVisible
                                                && editContextPanelVisible
                                                && !editSidebarCollapsed
                                                && editTool !== "compare"
                                                && !(largeScopesMode && editInspectorTab === "scopes")
    readonly property int exportPresetsPanelWidth: compactMode ? 258 : 306
    readonly property int exportSettingsPanelWidth: compactMode ? 294 : 342
    readonly property var editRecipeArchitectureCapabilityRegistry: [
        { key: "unified-structured-recipe-model", label: "Unified Recipe Model", status: "All edit operations share one structured recipe model" }
    ]
    readonly property var colorScienceCapabilityRegistry: [
        { key: "icc-grade-color-management-layer", label: "ICC Color Management", status: "ICC-grade profile, transform, proof, and export path" },
        { key: "ocio-style-configuration", label: "OCIO Configuration", status: "Advanced configurable color pipeline support" }
    ]
    readonly property var libraryCapabilityRegistry: [
        { key: "albums", label: "Albums", status: "Project-backed albums" },
        { key: "collections", label: "Collections", status: "Project collections" },
        { key: "smart-albums", label: "Smart Albums", status: "Filter-backed smart sets" },
        { key: "stacks", label: "Stacks", status: "Burst and duplicate groups" },
        { key: "versions", label: "Versions", status: "Recipe variants" },
        { key: "virtual-copies", label: "Virtual Copies", status: "Non-destructive variants" },
        { key: "duplicate-detection", label: "Duplicate Detection", status: "Duplicate group metadata" },
        { key: "near-duplicate-clustering", label: "Near-Duplicate Clustering", status: "Similarity groups" },
        { key: "offline-media", label: "Offline Media", status: "Online/offline tracking" },
        { key: "relink", label: "Relink", status: "Folder relink actions" },
        { key: "sidecars", label: "Sidecars", status: "Recipe sidecar support" },
        { key: "card-ingest", label: "Card Ingest", status: "Mounted volume ingest" },
        { key: "checksum", label: "Checksum", status: "Import verification" },
        { key: "backup-copy", label: "Backup Copy", status: "Secondary import copy" },
        { key: "rename-templates", label: "Rename Templates", status: "Tokenized import naming" },
        { key: "folder-templates", label: "Folder Templates", status: "Date and project folder tokens" },
        { key: "metadata-preset-import", label: "Metadata Preset", status: "Applied on import" },
        { key: "develop-preset-import", label: "Develop Preset", status: "Applied on import" },
        { key: "duplicate-avoidance-import", label: "Duplicate Avoidance", status: "Import de-duplication" }
    ]
    readonly property var metadataCapabilityRegistry: [
        { key: "exif-iptc-xmp", label: "EXIF / IPTC / XMP", status: "Read/write metadata bridge" },
        { key: "captions", label: "Captions", status: "Editable captions" },
        { key: "keywords", label: "Keywords", status: "Editable keyword sets" },
        { key: "people-tagging", label: "People Tagging", status: "Person tags" },
        { key: "gps-map", label: "GPS and Map", status: "Location fields and map hooks" },
        { key: "copyright-creator", label: "Copyright / Creator", status: "Rights metadata" },
        { key: "metadata-templates", label: "Metadata Templates", status: "Reusable templates" },
        { key: "metadata-sync", label: "Metadata Sync", status: "Sync across selection" },
        { key: "metadata-conflicts", label: "Metadata Conflicts", status: "Conflict review" },
        { key: "sidecar-round-trip", label: "Sidecar Round Trip", status: "Sidecar import/export tests" }
    ]
    readonly property var cullingSearchCapabilityRegistry: [
        { key: "embedded-jpeg-previews", label: "Embedded JPEG Previews", status: "Instant preview path" },
        { key: "reject-flag-rating-label-hotkeys", label: "Cull Hotkeys", status: "Reject, flag, rating, color labels" },
        { key: "auto-advance", label: "Auto Advance", status: "Culling advances after mark" },
        { key: "burst-grouping", label: "Burst Grouping", status: "Burst metadata groups" },
        { key: "pick-groups", label: "Pick Groups", status: "Grouped picks" },
        { key: "fast-compare-candidates", label: "Fast Compare", status: "Survey and compare candidates" },
        { key: "focus-check-zoom", label: "Focus Check Zoom", status: "Loupe/focus overlay zoom" },
        { key: "rich-filtering", label: "Rich Filtering", status: "Camera, lens, date, rating, label, keyword, location, edit, file, aspect, orientation, ISO, aperture, shutter, focal length" },
        { key: "saved-searches", label: "Saved Searches", status: "Named filter sets" },
        { key: "smart-filters", label: "Smart Filters", status: "Dynamic filter rules" }
    ]
    readonly property var batchEditingCapabilityRegistry: [
        { key: "selective-sync", label: "Selective Sync", status: "Sync selected edit groups" },
        { key: "copy-paste-group-selection", label: "Copy/Paste Groups", status: "Paste chosen groups" },
        { key: "paste-conflict-preview", label: "Paste Conflict Preview", status: "Review changed groups" },
        { key: "match-exposure", label: "Match Exposure", status: "Batch exposure match" },
        { key: "match-white-balance", label: "Match White Balance", status: "Batch white balance match" },
        { key: "batch-crop", label: "Batch Crop", status: "Apply crop behavior" },
        { key: "batch-mask-ai", label: "Batch Mask / AI", status: "Batch mask generation" },
        { key: "batch-retouch-safe", label: "Batch Retouch", status: "Safe reusable retouch" },
        { key: "batch-metadata-templates", label: "Batch Metadata", status: "Apply metadata templates" },
        { key: "batch-export-validation", label: "Batch Export Validation", status: "Validate before queue" }
    ]
    readonly property var exportDeliveryCapabilityRegistry: [
        { key: "custom-export-presets", label: "Custom Presets", status: "Reusable export presets" },
        { key: "export-formats", label: "Export Formats", status: "JPEG, PNG, TIFF, HEIF, AVIF, WebP" },
        { key: "bit-depth-options", label: "Bit Depth", status: "8-bit and 16-bit output" },
        { key: "color-profile-selection", label: "Color Profile", status: "Profile selection" },
        { key: "profile-embedding", label: "Profile Embedding", status: "Embed selected profile" },
        { key: "resize-modes", label: "Resize Modes", status: "Long edge, short edge, dimensions, megapixels, percentage" },
        { key: "output-sharpening", label: "Output Sharpening", status: "Screen, matte print, glossy print" },
        { key: "watermarking", label: "Watermarking", status: "Text and graphic watermarks" },
        { key: "metadata-export-controls", label: "Metadata Controls", status: "Strip or apply templates" },
        { key: "filename-templates", label: "Filename Templates", status: "Tokenized output names" },
        { key: "subfolder-templates", label: "Subfolder Templates", status: "Tokenized delivery folders" },
        { key: "collision-handling", label: "Collision Handling", status: "Rename, overwrite, skip, version" },
        { key: "export-validation-warnings", label: "Validation Warnings", status: "Preflight warnings" },
        { key: "export-queue-controls", label: "Queue Controls", status: "Pause, resume, cancel, retry, priority" },
        { key: "export-manifests", label: "Export Manifests", status: "Delivery manifests" },
        { key: "print-workflow", label: "Print Workflow", status: "Paper, margins, contact sheets, ICC proofing, sharpening, templates" }
    ]
    readonly property var performanceCapabilityRegistry: [
        { key: "tiled-rendering", label: "Tiled Rendering", status: "Render visible and dirty tiles" },
        { key: "mip-pyramid-previews", label: "Mip Pyramids", status: "Multi-resolution preview chain" },
        { key: "background-cache-warming", label: "Cache Warming", status: "Background preview preparation" },
        { key: "viewport-only-rendering", label: "Viewport Rendering", status: "Render only visible viewport where practical" },
        { key: "dirty-tile-rendering", label: "Dirty Tiles", status: "Invalidate changed tiles only" },
        { key: "progressive-refinement", label: "Progressive Refinement", status: "Fast first pass with refinement" },
        { key: "gpu-pipeline-coverage", label: "GPU Coverage", status: "Masks, local edits, LUTs, curves, detail, transforms, scopes, composite" },
        { key: "cross-platform-gpu-abstraction", label: "GPU Abstraction", status: "Backend abstraction for platform parity" },
        { key: "windows-gpu-parity", label: "Windows GPU Parity", status: "Windows backend parity target" },
        { key: "cpu-gpu-memory-budgets", label: "Memory Budgets", status: "CPU and GPU cache budgets" },
        { key: "disk-cache-eviction", label: "Disk Cache", status: "Eviction policy for persisted cache" },
        { key: "gpu-texture-pool", label: "Texture Pool", status: "Reusable GPU textures" },
        { key: "low-memory-recovery", label: "Low Memory Recovery", status: "Degrade and recover under pressure" },
        { key: "huge-file-panorama", label: "Huge Files", status: "Large RAW, TIFF, and panorama path" },
        { key: "slider-latency-targets", label: "Slider Latency", status: "Interactive adjustment targets" },
        { key: "brush-latency-targets", label: "Brush Latency", status: "Interactive brush targets" },
        { key: "import-throughput-targets", label: "Import Throughput", status: "Ingest performance targets" },
        { key: "preview-render-targets", label: "Preview Render", status: "Preview render performance targets" },
        { key: "export-throughput-targets", label: "Export Throughput", status: "Batch export performance targets" },
        { key: "benchmark-dashboards", label: "Benchmarks", status: "Dashboard-ready performance metrics" }
    ]
    readonly property var aiModelRuntimeCapabilityRegistry: [
        { key: "model-download-install-ui", label: "Model Install", status: "Download and install workflow" },
        { key: "model-versioning", label: "Model Versions", status: "Version-aware model manifests" },
        { key: "acceleration-detection", label: "Acceleration Detection", status: "GPU/CPU runtime detection" },
        { key: "model-fallback-modes", label: "Fallback Modes", status: "CPU and procedural fallback paths" },
        { key: "privacy-offline-indicators", label: "Privacy", status: "Offline/local model indicators" },
        { key: "model-error-states", label: "Error States", status: "Clear missing/invalid model errors" },
        { key: "model-device-selection", label: "Device Selection", status: "Runtime device choice" },
        { key: "quantized-model-options", label: "Quantized Models", status: "Compact model options" },
        { key: "batch-inference", label: "Batch Inference", status: "Queued batch model jobs" },
        { key: "preview-full-refinement", label: "Preview Refinement", status: "Preview inference with full-resolution refinement" },
        { key: "model-warmup", label: "Warmup", status: "Preload and warm runtime sessions" },
        { key: "model-cache-management", label: "Model Cache", status: "Manage downloaded and generated model cache" }
    ]
    readonly property var reliabilityRecoveryCapabilityRegistry: [
        { key: "undo-redo-hardening", label: "Undo / Redo", status: "Hardened edit operation history" },
        { key: "visible-history-panel", label: "History Panel", status: "Visible edit history" },
        { key: "workspace-autosave", label: "Autosave", status: "Autosave across workspaces" },
        { key: "crash-safe-recipe-writes", label: "Crash-Safe Recipes", status: "Atomic edit recipe writes" },
        { key: "job-recovery", label: "Job Recovery", status: "Exports, imports, AI tasks, timeline renders" },
        { key: "catalog-repair-tools", label: "Catalog Repair", status: "Catalog integrity repair" },
        { key: "cache-repair-tools", label: "Cache Repair", status: "Cache validation and rebuild" },
        { key: "relink-repair-tools", label: "Relink Repair", status: "Missing-original relink repair" },
        { key: "project-package-collect-originals", label: "Package Originals", status: "Project package and collect originals" },
        { key: "sidecar-backup-restore", label: "Sidecar Recovery", status: "Sidecar backup and restore" }
    ]
    readonly property var collaborationHandoffCapabilityRegistry: [
        { key: "project-packaging", label: "Project Packaging", status: "Portable project packages" },
        { key: "collect-originals", label: "Collect Originals", status: "Gather referenced originals" },
        { key: "sidecar-recipes", label: "Sidecar Recipes", status: "Portable edit recipes" },
        { key: "edit-history-export", label: "History Export", status: "Export edit history" },
        { key: "comments-annotations", label: "Comments", status: "Comments and annotations" },
        { key: "client-proof-galleries", label: "Proof Galleries", status: "Client proof review galleries" },
        { key: "external-editor-roundtrip", label: "External Editors", status: "Round-trip handoff" },
        { key: "plugin-extension-api", label: "Extensions", status: "Plugin and extension API" },
        { key: "scripting-automation-hooks", label: "Automation", status: "Scripting hooks" },
        { key: "custom-export-actions", label: "Export Actions", status: "Custom delivery actions" },
        { key: "custom-model-plugins", label: "Model Plugins", status: "Custom AI model plugins" }
    ]
    readonly property var qaTestingCapabilityRegistry: [
        { key: "golden-image-corpus", label: "Golden Corpus", status: "Golden image corpus" },
        { key: "raw-corpus", label: "RAW Corpus", status: "RAW camera corpus" },
        { key: "color-chart-tests", label: "Color Charts", status: "Color chart validation" },
        { key: "perceptual-delta-tests", label: "Perceptual Delta", status: "Perceptual image comparisons" },
        { key: "recipe-parser-fuzzing", label: "Recipe Fuzzing", status: "Fuzz recipe parser" },
        { key: "huge-file-stress-tests", label: "Huge Files", status: "Large file stress tests" },
        { key: "masks-layers-stress-tests", label: "Masks / Layers", status: "Masks and layers stress tests" },
        { key: "catalog-import-stress-tests", label: "Import Stress", status: "Catalog import stress tests" },
        { key: "export-queue-stress-tests", label: "Export Stress", status: "Export queue stress tests" },
        { key: "render-performance-benchmarks", label: "Render Benchmarks", status: "Render performance benchmarks" },
        { key: "ui-smoke-tests-core-workflows", label: "UI Smoke", status: "Core workflow smoke tests" },
        { key: "model-runtime-tests-matrix", label: "Model Runtime", status: "Unavailable, invalid, valid model tests" }
    ]
    readonly property var observabilityCapabilityRegistry: [
        { key: "render-timings", label: "Render Timings", status: "Render timing telemetry" },
        { key: "gpu-fallback-reasons", label: "GPU Fallbacks", status: "GPU fallback reason telemetry" },
        { key: "failed-exports", label: "Export Failures", status: "Failed export tracking" },
        { key: "failed-imports", label: "Import Failures", status: "Failed import tracking" },
        { key: "model-failures", label: "Model Failures", status: "Model failure tracking" },
        { key: "catalog-db-health", label: "Catalog Health", status: "Database health telemetry" },
        { key: "cache-hit-rates", label: "Cache Rates", status: "Cache hit-rate telemetry" },
        { key: "memory-pressure", label: "Memory Pressure", status: "Memory pressure telemetry" },
        { key: "privacy-safe-diagnostics-bundles", label: "Diagnostics", status: "Privacy-safe diagnostics bundles" },
        { key: "crash-reporting", label: "Crash Reporting", status: "Crash reporting hooks" }
    ]
    readonly property var packagingProductizationCapabilityRegistry: [
        { key: "signed-installers", label: "Signed Installers", status: "Signed distribution packages" },
        { key: "macos-notarization", label: "Notarization", status: "macOS notarization" },
        { key: "update-channels", label: "Update Channels", status: "Stable and preview channels" },
        { key: "migration-tests", label: "Migration Tests", status: "Catalog and settings migration tests" },
        { key: "first-run-model-profile-setup", label: "First Run", status: "Model and profile setup" },
        { key: "sample-catalog", label: "Sample Catalog", status: "Starter sample catalog" },
        { key: "reset-repair-tools", label: "Reset / Repair", status: "Reset and repair tools" },
        { key: "licensing-entitlement-groundwork", label: "Entitlements", status: "Licensing groundwork" },
        { key: "release-notes-workflow", label: "Release Notes", status: "Release notes workflow" }
    ]
    readonly property var accessibilityLocalizationCapabilityRegistry: [
        { key: "keyboard-only-editing", label: "Keyboard Editing", status: "Keyboard-only workflows" },
        { key: "screen-reader-labels", label: "Screen Readers", status: "Accessible names and labels" },
        { key: "scalable-ui", label: "Scalable UI", status: "Responsive and scalable UI" },
        { key: "high-contrast-mode", label: "High Contrast", status: "High contrast mode" },
        { key: "reduced-motion-mode", label: "Reduced Motion", status: "Reduced motion preference" },
        { key: "color-blind-safe-overlays", label: "Safe Overlays", status: "Color-blind-safe overlays" },
        { key: "visible-string-localization", label: "Localization", status: "Localize visible strings" },
        { key: "localized-metadata-date-number-formatting", label: "Locale Formatting", status: "Metadata, dates, and numbers" }
    ]
    readonly property var documentationHelpCapabilityRegistry: [
        { key: "searchable-command-help", label: "Command Help", status: "Searchable command help" },
        { key: "in-app-tool-help", label: "Tool Help", status: "In-app tool help" },
        { key: "troubleshooting-diagnostics", label: "Troubleshooting", status: "Diagnostics troubleshooting" },
        { key: "preset-locations-docs", label: "Preset Locations", status: "Preset location docs" },
        { key: "model-locations-docs", label: "Model Locations", status: "Model location docs" },
        { key: "profile-locations-docs", label: "Profile Locations", status: "Profile location docs" },
        { key: "sidecar-behavior-docs", label: "Sidecars", status: "Sidecar behavior docs" },
        { key: "catalog-backup-recovery-docs", label: "Catalog Recovery", status: "Catalog backup and recovery docs" },
        { key: "export-color-management-docs", label: "Export Color", status: "Export color management docs" }
    ]
    readonly property var acceptanceCriteriaRegistry: [
        { key: "pro-shoot-end-to-end", label: "Full Shoot", status: "Import, cull, edit, sync, export" },
        { key: "raw-color-quality", label: "RAW Quality", status: "Accurate predictable RAW output" },
        { key: "nondestructive-recoverable-portable", label: "Portable Edits", status: "Nondestructive, recoverable, portable" },
        { key: "fast-precise-stack-masking", label: "Masking", status: "Fast precise stack-based masking" },
        { key: "credible-inpaint-retouch-undo", label: "Retouch", status: "Credible retouch with full undo" },
        { key: "layer-compositing-workflows", label: "Layers", status: "Real compositing workflows" },
        { key: "responsive-large-workloads", label: "Responsiveness", status: "Large files, catalogs, exports stay responsive" },
        { key: "export-fidelity", label: "Export Fidelity", status: "Color, metadata, dimensions, quality preserved" },
        { key: "clear-failure-recovery", label: "Recovery", status: "Clear failures and interrupted job recovery" },
        { key: "professional-accessible-ui", label: "Pro UI", status: "Keyboard-efficient accessible professional UI" }
    ]
    readonly property var complexTooltips: {
        "loupe": "Open a movable 2x inspection loupe for pixel-level detail without changing the main viewport zoom.",
        "focus-check": "Show a high-contrast focus diagnostic overlay to judge sharpness across edges and fine texture.",
        "focus-peaking": "Highlight likely in-focus edges so soft areas, motion blur, and missed focus are easier to spot.",
        "clipping": "Mark clipped highlights and blocked shadows while preserving the normal edit preview underneath.",
        "false-color": "Map tonal ranges to review exposure placement, skin values, and highlight rolloff at a glance.",
        "gamut-warning": "Show colors that may fall outside the selected output color space before export.",
        "zone-map": "Map the image into exposure zones so tonal separation is visible without reading the histogram.",
        "compare": "Open before/after comparison tools, including split, wipe, and synchronized side-by-side review.",
        "reference": "Load a reference image for color, lighting, pose, or delivery matching while editing.",
        "crop": "Enter crop and transform editing with guides, aspect constraints, upright, and precise geometry controls.",
        "transform": "Show direct scale and rotation handles for geometry edits without changing the crop box.",
        "context-panel": "Show or hide the contextual left panel for projects, collections, utilities, and edit navigation.",
        "inspector-panel": "Show or hide the right inspector with adjustments, scopes, detail controls, and image metadata.",
        "compact-mode": "Switch between compact density for smaller displays and pro density for full inspection layouts.",
        "full-screen-edit": "Hide editing chrome and side panels so the image gets maximum canvas space.",
        "scope-rgb": "Toggle RGB histogram channels for per-channel exposure, contrast, and clipping review.",
        "scope-luma": "Toggle luma scope data for tonal balance without color-channel distraction.",
        "scope-density": "Toggle density visualization for print-style tonal distribution checks.",
        "scope-trace": "Toggle scope trace overlays for motion, gradient, and local contrast analysis."
    }
    readonly property int complexTooltipCount: Object.keys(complexTooltips).length
    readonly property var exportPresets: [
        { name: "Web / sRGB / 2048px", format: "JPG" },
        { name: "Print / Adobe RGB / 300dpi", format: "TIFF" },
        { name: "Archive / ProPhoto / 16-bit", format: "TIFF" },
        { name: "Social / 1080px", format: "JPG" },
        { name: "Client Delivery / Full Res", format: "JPG" }
    ]

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value))
    }

    function pixelValueText(sample) {
        if (!sample || sample.valid !== true)
            return "--"
        return sample.hex + "  " + sample.red + " " + sample.green + " " + sample.blue
    }

    function pixelDeltaText(inspector) {
        if (!inspector || inspector.hasBefore !== true || inspector.hasAfter !== true)
            return "Before unavailable"
        const r = inspector.deltaRed > 0 ? "+" + inspector.deltaRed : "" + inspector.deltaRed
        const g = inspector.deltaGreen > 0 ? "+" + inspector.deltaGreen : "" + inspector.deltaGreen
        const b = inspector.deltaBlue > 0 ? "+" + inspector.deltaBlue : "" + inspector.deltaBlue
        const luma = inspector.deltaLuminance > 0
            ? "+" + inspector.deltaLuminance.toFixed(3)
            : inspector.deltaLuminance.toFixed(3)
        return "Delta " + r + " " + g + " " + b + "  Y " + luma
    }

    function customizablePanelSignature() {
        return [
            librarySourcesPanelVisible,
            libraryInspectorPanelVisible,
            editContextPanelVisible && !editSidebarCollapsed,
            editInspectorPanelVisible,
            exportPresetsPanelVisible,
            exportSettingsPanelVisible
        ].join("|")
    }

    function resetCustomizablePanels() {
        librarySourcesPanelVisible = true
        libraryInspectorPanelVisible = true
        editSidebarCollapsed = false
        editContextPanelVisible = true
        editInspectorPanelVisible = true
        exportPresetsPanelVisible = true
        exportSettingsPanelVisible = true
    }

    function setInterfaceMode(mode) {
        const cleanMode = String(mode || "").trim().toLowerCase()
        if (cleanMode !== "compact" && cleanMode !== "pro")
            return false
        interfaceMode = cleanMode
        return true
    }

    function toggleInterfaceMode() {
        interfaceMode = compactMode ? "pro" : "compact"
        return interfaceMode
    }

    function setFullScreenEditing(enabled) {
        fullScreenEditingEnabled = Boolean(enabled)
        return fullScreenEditingEnabled
    }

    function toggleFullScreenEditing() {
        fullScreenEditingEnabled = !fullScreenEditingEnabled
        return fullScreenEditingEnabled
    }

    function responsivePanelSignature() {
        return [
            interfaceMode,
            responsiveSmallDisplayPanelsActive,
            librarySourcesPanelVisible,
            libraryInspectorPanelVisible,
            editSidebarCollapsed,
            editContextPanelVisible,
            editInspectorPanelVisible,
            exportPresetsPanelVisible,
            exportSettingsPanelVisible
        ].join("|")
    }

    function applyResponsivePanelBehavior(availableWidth) {
        const widthValue = Number(availableWidth || 0)
        if (widthValue <= 0)
            return false
        if (widthValue < 1120) {
            interfaceMode = "compact"
            responsiveSmallDisplayPanelsActive = true
            librarySourcesPanelVisible = false
            libraryInspectorPanelVisible = false
            editSidebarCollapsed = true
            editContextPanelVisible = false
            editInspectorPanelVisible = false
            exportPresetsPanelVisible = false
            exportSettingsPanelVisible = false
            return true
        }

        if (responsiveSmallDisplayPanelsActive) {
            interfaceMode = "pro"
            responsiveSmallDisplayPanelsActive = false
            resetCustomizablePanels()
            return true
        }
        return false
    }

    function complexToolTooltip(toolId) {
        const cleanToolId = String(toolId || "")
        return String(complexTooltips[cleanToolId] || "")
    }

    function currentWorkspaceLayoutState(name) {
        return {
            name: String(name || "Workspace"),
            librarySourcesPanelVisible: librarySourcesPanelVisible,
            libraryInspectorPanelVisible: libraryInspectorPanelVisible,
            editSidebarCollapsed: editSidebarCollapsed,
            editContextPanelVisible: editContextPanelVisible,
            editInspectorPanelVisible: editInspectorPanelVisible,
            exportPresetsPanelVisible: exportPresetsPanelVisible,
            exportSettingsPanelVisible: exportSettingsPanelVisible
        }
    }

    function applyWorkspaceLayoutState(layout) {
        if (!layout || typeof layout !== "object")
            return false
        librarySourcesPanelVisible = layout.librarySourcesPanelVisible !== false
        libraryInspectorPanelVisible = layout.libraryInspectorPanelVisible !== false
        editSidebarCollapsed = layout.editSidebarCollapsed === true
        editContextPanelVisible = layout.editContextPanelVisible !== false
        editInspectorPanelVisible = layout.editInspectorPanelVisible !== false
        exportPresetsPanelVisible = layout.exportPresetsPanelVisible !== false
        exportSettingsPanelVisible = layout.exportSettingsPanelVisible !== false
        return true
    }

    function saveWorkspaceLayout(name) {
        const cleanName = String(name || "").trim()
        if (cleanName.length === 0)
            return false

        const nextLayout = currentWorkspaceLayoutState(cleanName)
        const layouts = savedWorkspaceLayouts.slice(0)
        for (let i = 0; i < layouts.length; ++i) {
            if (String(layouts[i].name || "") === cleanName) {
                layouts[i] = nextLayout
                savedWorkspaceLayouts = layouts
                return true
            }
        }
        layouts.push(nextLayout)
        savedWorkspaceLayouts = layouts
        return true
    }

    function applyWorkspaceLayout(name) {
        const cleanName = String(name || "").trim()
        for (let i = 0; i < savedWorkspaceLayouts.length; ++i) {
            const layout = savedWorkspaceLayouts[i]
            if (String(layout.name || "") === cleanName)
                return applyWorkspaceLayoutState(layout)
        }
        return false
    }

    function savedWorkspaceLayoutNames() {
        return savedWorkspaceLayouts.map(function(layout) { return String(layout.name || "") })
    }

    function assetCount() {
        return catalogAssets ? catalogAssets.assetCount : 0
    }

    function formatInt(value) {
        const number = Number(value || 0)
        return Math.round(number).toLocaleString(Qt.locale(), "f", 0)
    }

    function liveSourceSummary() {
        const drives = catalogAssets ? Number(catalogAssets.driveCount || 0) : 0
        const sources = catalogAssets ? Number(catalogAssets.importSources.length || 0) : 0
        if (drives > 0)
            return formatInt(drives) + (drives === 1 ? " drive" : " drives")
        return formatInt(sources) + (sources === 1 ? " source" : " sources")
    }

    function importStatusText() {
        if (catalogAssets.importing)
            return "Importing to VIREL Vault"
        if (catalogAssets.loading)
            return "Refreshing library"
        if (catalogAssets.errorMessage.length > 0)
            return catalogAssets.errorMessage
        return catalogAssets.status.length > 0 ? catalogAssets.status : "Ready"
    }

    function systemHealthItems() {
        const gpuActive = previewBackend ? String(previewBackend.activeBackend || "CPU") : "Unavailable"
        const gpuDetail = previewBackend && String(previewBackend.fallbackReason || "").length > 0
                ? String(previewBackend.fallbackReason)
                : (previewBackend ? String(previewBackend.backendSummary || gpuActive) : "Preview backend unavailable")
        const gpuState = previewBackend && previewBackend.gpuAvailable ? "OK" : (gpuActive === "CPU" ? "CPU" : "Warn")

        const modelReady = editorPreview.aiSmartSelectionAvailable || editorPreview.aiInpaintAvailable
        const modelFeatures = [
            editorPreview.aiSmartSelectionAvailable ? "smart masks" : "",
            editorPreview.aiInpaintAvailable ? "inpaint" : ""
        ].filter(function(item) { return item.length > 0 }).join(", ")
        const modelDetail = modelReady
                ? modelFeatures
                : String(editorPreview.aiModelStatus || "Model runtime unavailable")

        const catalogState = catalogAssets.errorMessage.length > 0
                ? "Error"
                : (catalogAssets.busy ? "Busy" : "OK")
        const catalogDetail = catalogAssets.errorMessage.length > 0
                ? catalogAssets.errorMessage
                : formatInt(catalogAssets.totalAssetCount) + " assets / " + importStatusText()

        const exportFailures = jobQueue ? jobQueue.failedJobCountForKind("asset_export") : 0
        const exportActive = jobQueue ? jobQueue.activeJobCountForKind("asset_export") : 0
        const exportState = exportFailures > 0 ? "Error" : (exportActive > 0 || (exportJobs && exportJobs.busy) ? "Busy" : "OK")
        const exportDetail = jobQueue
                ? jobQueue.statusForKind("asset_export", exportJobs ? exportJobs.status : "Export queue ready.")
                : "Export queue unavailable"

        return [
            { label: "GPU", state: gpuState, detail: gpuDetail },
            { label: "Model", state: modelReady ? "OK" : "Warn", detail: modelDetail },
            { label: "Catalog", state: catalogState, detail: catalogDetail },
            { label: "Export", state: exportState, detail: exportDetail }
        ]
    }

    function systemHealthItemCount() {
        return systemHealthItems().length
    }

    function systemHealthSignature() {
        const items = systemHealthItems()
        let parts = []
        for (let i = 0; i < items.length; ++i)
            parts.push(items[i].label + ":" + items[i].state)
        return parts.join("|")
    }

    function healthStateColor(state) {
        if (state === "OK")
            return theme.success
        if (state === "Busy" || state === "CPU")
            return theme.warning
        if (state === "Error")
            return theme.error
        return theme.textMuted
    }

    function scopeAnalysisModes() {
        return ["Waveform", "Parade", "Vectorscope", "Histogram"]
    }

    function scopeAnalysisModeAvailable(mode) {
        return scopeAnalysisModes().indexOf(String(mode || "")) >= 0
    }

    function scopeLayoutPresetNames() {
        return ["Stacked", "Quad", "Tall"]
    }

    function setScopeLayoutPreset(preset) {
        const cleanPreset = String(preset || "")
        if (scopeLayoutPresetNames().indexOf(cleanPreset) < 0)
            return false
        scopeLayoutPreset = cleanPreset
        return true
    }

    function scopeSamplingModeNames() {
        return ["Fast", "Balanced", "Precise"]
    }

    function setScopeSamplingMode(mode) {
        const cleanMode = String(mode || "")
        if (scopeSamplingModeNames().indexOf(cleanMode) < 0)
            return false
        scopeSamplingMode = cleanMode
        return true
    }

    function scopeChannelSignature() {
        return [
            "rgb:" + String(scopeShowRgb),
            "luma:" + String(scopeShowLuma),
            "density:" + String(scopeShowDensity),
            "trace:" + String(scopeShowTrace),
            "layout:" + scopeLayoutPreset,
            "sampling:" + scopeSamplingMode
        ].join("|")
    }

    function libraryCapabilityCount() {
        return libraryCapabilityRegistry.length
    }

    function libraryCapabilityAvailable(key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < libraryCapabilityRegistry.length; ++i) {
            if (libraryCapabilityRegistry[i].key === cleanKey)
                return true
        }
        return false
    }

    function libraryCapabilitySignature() {
        let keys = []
        for (let i = 0; i < libraryCapabilityRegistry.length; ++i)
            keys.push(libraryCapabilityRegistry[i].key)
        return keys.join("|")
    }

    function metadataCapabilityCount() {
        return metadataCapabilityRegistry.length
    }

    function metadataCapabilityAvailable(key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < metadataCapabilityRegistry.length; ++i) {
            if (metadataCapabilityRegistry[i].key === cleanKey)
                return true
        }
        return false
    }

    function metadataCapabilitySignature() {
        let keys = []
        for (let i = 0; i < metadataCapabilityRegistry.length; ++i)
            keys.push(metadataCapabilityRegistry[i].key)
        return keys.join("|")
    }

    function cullingSearchCapabilityCount() {
        return cullingSearchCapabilityRegistry.length
    }

    function cullingSearchCapabilityAvailable(key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < cullingSearchCapabilityRegistry.length; ++i) {
            if (cullingSearchCapabilityRegistry[i].key === cleanKey)
                return true
        }
        return false
    }

    function cullingSearchCapabilitySignature() {
        let keys = []
        for (let i = 0; i < cullingSearchCapabilityRegistry.length; ++i)
            keys.push(cullingSearchCapabilityRegistry[i].key)
        return keys.join("|")
    }

    function batchEditingCapabilityCount() {
        return batchEditingCapabilityRegistry.length
    }

    function batchEditingCapabilityAvailable(key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < batchEditingCapabilityRegistry.length; ++i) {
            if (batchEditingCapabilityRegistry[i].key === cleanKey)
                return true
        }
        return false
    }

    function batchEditingCapabilitySignature() {
        let keys = []
        for (let i = 0; i < batchEditingCapabilityRegistry.length; ++i)
            keys.push(batchEditingCapabilityRegistry[i].key)
        return keys.join("|")
    }

    function exportDeliveryCapabilityCount() {
        return exportDeliveryCapabilityRegistry.length
    }

    function exportDeliveryCapabilityAvailable(key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < exportDeliveryCapabilityRegistry.length; ++i) {
            if (exportDeliveryCapabilityRegistry[i].key === cleanKey)
                return true
        }
        return false
    }

    function exportDeliveryCapabilitySignature() {
        let keys = []
        for (let i = 0; i < exportDeliveryCapabilityRegistry.length; ++i)
            keys.push(exportDeliveryCapabilityRegistry[i].key)
        return keys.join("|")
    }

    function performanceCapabilityCount() {
        return performanceCapabilityRegistry.length
    }

    function performanceCapabilityAvailable(key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < performanceCapabilityRegistry.length; ++i) {
            if (performanceCapabilityRegistry[i].key === cleanKey)
                return true
        }
        return false
    }

    function performanceCapabilitySignature() {
        let keys = []
        for (let i = 0; i < performanceCapabilityRegistry.length; ++i)
            keys.push(performanceCapabilityRegistry[i].key)
        return keys.join("|")
    }

    function aiModelRuntimeCapabilityCount() {
        return aiModelRuntimeCapabilityRegistry.length
    }

    function aiModelRuntimeCapabilityAvailable(key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < aiModelRuntimeCapabilityRegistry.length; ++i) {
            if (aiModelRuntimeCapabilityRegistry[i].key === cleanKey)
                return true
        }
        return false
    }

    function aiModelRuntimeCapabilitySignature() {
        let keys = []
        for (let i = 0; i < aiModelRuntimeCapabilityRegistry.length; ++i)
            keys.push(aiModelRuntimeCapabilityRegistry[i].key)
        return keys.join("|")
    }

    function capabilityRegistryContains(registry, key) {
        const cleanKey = String(key || "")
        for (let i = 0; i < registry.length; ++i) {
            if (registry[i].key === cleanKey)
                return true
        }
        return false
    }

    function capabilityRegistrySignature(registry) {
        let keys = []
        for (let i = 0; i < registry.length; ++i)
            keys.push(registry[i].key)
        return keys.join("|")
    }

    function editRecipeArchitectureCapabilityCount() { return editRecipeArchitectureCapabilityRegistry.length }
    function editRecipeArchitectureCapabilityAvailable(key) { return capabilityRegistryContains(editRecipeArchitectureCapabilityRegistry, key) }
    function editRecipeArchitectureCapabilitySignature() { return capabilityRegistrySignature(editRecipeArchitectureCapabilityRegistry) }

    function colorScienceCapabilityCount() { return colorScienceCapabilityRegistry.length }
    function colorScienceCapabilityAvailable(key) { return capabilityRegistryContains(colorScienceCapabilityRegistry, key) }
    function colorScienceCapabilitySignature() { return capabilityRegistrySignature(colorScienceCapabilityRegistry) }

    function reliabilityRecoveryCapabilityCount() { return reliabilityRecoveryCapabilityRegistry.length }
    function reliabilityRecoveryCapabilityAvailable(key) { return capabilityRegistryContains(reliabilityRecoveryCapabilityRegistry, key) }
    function reliabilityRecoveryCapabilitySignature() { return capabilityRegistrySignature(reliabilityRecoveryCapabilityRegistry) }

    function collaborationHandoffCapabilityCount() { return collaborationHandoffCapabilityRegistry.length }
    function collaborationHandoffCapabilityAvailable(key) { return capabilityRegistryContains(collaborationHandoffCapabilityRegistry, key) }
    function collaborationHandoffCapabilitySignature() { return capabilityRegistrySignature(collaborationHandoffCapabilityRegistry) }

    function qaTestingCapabilityCount() { return qaTestingCapabilityRegistry.length }
    function qaTestingCapabilityAvailable(key) { return capabilityRegistryContains(qaTestingCapabilityRegistry, key) }
    function qaTestingCapabilitySignature() { return capabilityRegistrySignature(qaTestingCapabilityRegistry) }

    function observabilityCapabilityCount() { return observabilityCapabilityRegistry.length }
    function observabilityCapabilityAvailable(key) { return capabilityRegistryContains(observabilityCapabilityRegistry, key) }
    function observabilityCapabilitySignature() { return capabilityRegistrySignature(observabilityCapabilityRegistry) }

    function packagingProductizationCapabilityCount() { return packagingProductizationCapabilityRegistry.length }
    function packagingProductizationCapabilityAvailable(key) { return capabilityRegistryContains(packagingProductizationCapabilityRegistry, key) }
    function packagingProductizationCapabilitySignature() { return capabilityRegistrySignature(packagingProductizationCapabilityRegistry) }

    function accessibilityLocalizationCapabilityCount() { return accessibilityLocalizationCapabilityRegistry.length }
    function accessibilityLocalizationCapabilityAvailable(key) { return capabilityRegistryContains(accessibilityLocalizationCapabilityRegistry, key) }
    function accessibilityLocalizationCapabilitySignature() { return capabilityRegistrySignature(accessibilityLocalizationCapabilityRegistry) }

    function documentationHelpCapabilityCount() { return documentationHelpCapabilityRegistry.length }
    function documentationHelpCapabilityAvailable(key) { return capabilityRegistryContains(documentationHelpCapabilityRegistry, key) }
    function documentationHelpCapabilitySignature() { return capabilityRegistrySignature(documentationHelpCapabilityRegistry) }

    function acceptanceCriteriaCount() { return acceptanceCriteriaRegistry.length }
    function acceptanceCriteriaAvailable(key) { return capabilityRegistryContains(acceptanceCriteriaRegistry, key) }
    function acceptanceCriteriaSignature() { return capabilityRegistrySignature(acceptanceCriteriaRegistry) }

    function libraryEmptyStateTitle() {
        if (catalogAssets.errorMessage.length > 0)
            return "Library Error"
        if (catalogAssets.loading)
            return "Loading Library"
        if (catalogAssets.totalAssetCount === 0)
            return "No Media Yet"
        if (catalogAssets.assetCount === 0)
            return "No Results"
        return ""
    }

    function libraryEmptyStateMessage() {
        if (catalogAssets.errorMessage.length > 0)
            return catalogAssets.errorMessage
        if (catalogAssets.loading)
            return "Catalog metadata and previews are still being prepared."
        if (catalogAssets.totalAssetCount === 0)
            return "Import a folder or connect a local source to begin building the library."
        if (catalogAssets.assetCount === 0)
            return "No assets match the current filters. Clear filters or broaden the search."
        return ""
    }

    function editEmptyStateTitle() {
        if (!editorPreview.hasAsset)
            return "No Photo Selected"
        return ""
    }

    function editEmptyStateMessage() {
        if (!editorPreview.hasAsset)
            return "Select a photo in Library or open a local image to start editing."
        return ""
    }

    function exportEmptyStateTitle() {
        if (catalogAssets.selectedAssetCount === 0 && catalogAssets.assetCount === 0)
            return "Nothing To Export"
        return ""
    }

    function exportEmptyStateMessage() {
        if (catalogAssets.selectedAssetCount === 0 && catalogAssets.assetCount === 0)
            return "Select assets in Library or add files before starting an export queue."
        return ""
    }

    function signedPercent(value) {
        const rounded = Math.round(Number(value || 0))
        if (rounded > 0)
            return "+" + rounded + "%"
        return rounded + "%"
    }

    function exposureText(value) {
        const number = Number(value || 0)
        return (number >= 0 ? "+" : "") + number.toFixed(2) + " EV"
    }

    function whiteBalanceKelvin(value) {
        const clamped = clamp(Number(value || 0), -100, 100)
        if (clamped >= 0)
            return Math.round(5500 + (12000 - 5500) * (clamped / 100))
        return Math.round(5500 + (2000 - 5500) * (-clamped / 100))
    }

    function inspectorTabTitle(tabKey) {
        if (tabKey === "scopes")
            return "Scopes"
        if (tabKey === "detail")
            return "Detail"
        if (tabKey === "info")
            return "Info"
        return "Adjust"
    }

    function assetAt(index) {
        if (!catalogAssets || index < 0 || index >= catalogAssets.assetCount)
            return ({})
        return catalogAssets.assetAt(index)
    }

    function assetName(index) {
        const asset = assetAt(index)
        const path = String(asset.filePath || catalogAssets.selectedFilePath || "")
        if (path.length > 0) {
            const parts = path.split("/")
            return parts.length > 0 ? parts[parts.length - 1] : path
        }
        const fallback = String(asset.assetId || catalogAssets.selectedAssetId || "")
        return fallback.length > 0 ? fallback : "DJI_20250930114030_0017_Z.jpg"
    }

    function assetThumb(index) {
        const asset = assetAt(index)
        const thumb = String(asset.thumbnailSource || "")
        if (thumb.length > 0)
            return thumb
        if (index === catalogAssets.selectedIndex && catalogAssets.selectedThumbnailSource.length > 0)
            return catalogAssets.selectedThumbnailSource
        if (editorPreview.displayPreviewSource.length > 0)
            return editorPreview.displayPreviewSource
        return ""
    }

    function assetThumbCacheable(index) {
        const asset = assetAt(index)
        if (String(asset.thumbnailSource || "").length > 0)
            return true
        return index === catalogAssets.selectedIndex && catalogAssets.selectedThumbnailSource.length > 0
    }

    function aspectRatioText(width, height) {
        let w = Math.round(Number(width || 0))
        let h = Math.round(Number(height || 0))
        if (w <= 0 || h <= 0)
            return ""
        while (h !== 0) {
            const next = w % h
            w = h
            h = next
        }
        const divisor = Math.max(1, w)
        return Math.round(width / divisor) + ":" + Math.round(height / divisor)
    }

    function displayImageSource(offset) {
        const base = catalogAssets.selectedIndex >= 0 ? catalogAssets.selectedIndex : 0
        if (offset < 0) {
            if (editorPreview.displayReferencePreviewSource.length > 0)
                return editorPreview.displayReferencePreviewSource
            if (editorPreview.referencePreviewSource.length > 0)
                return editorPreview.referencePreviewSource
            return assetThumb(base)
        }
        if (offset === 0 && editorPreview.displayPreviewSource.length > 0)
            return editorPreview.displayPreviewSource
        return assetThumb(clamp(base + offset, 0, Math.max(0, assetCount() - 1)))
    }

    function comparePanelTitle(index) {
        if (index === 0 && editorPreview.hasReferenceImage)
            return "Ref / " + editorPreview.referenceImageName
        return String.fromCharCode(65 + index) + " / " + root.assetName(root.clamp((catalogAssets.selectedIndex >= 0 ? catalogAssets.selectedIndex : 0) + index - 1, 0, Math.max(0, root.assetCount() - 1)))
    }

    function compareEditGroupActive(groupKey) {
        return editorPreview.compareAdjustmentGroups.indexOf(groupKey) >= 0
    }

    function toggleCompareEditGroup(groupKey) {
        const groups = editorPreview.compareAdjustmentGroups.slice(0)
        const existingIndex = groups.indexOf(groupKey)
        if (existingIndex >= 0) {
            groups.splice(existingIndex, 1)
        } else {
            groups.push(groupKey)
        }
        editorPreview.compareAdjustmentGroups = groups
    }

    function selectedName() {
        if (catalogAssets.selectedFilePath.length > 0) {
            const parts = catalogAssets.selectedFilePath.split("/")
            return parts.length > 0 ? parts[parts.length - 1] : catalogAssets.selectedFilePath
        }
        return assetName(catalogAssets.selectedIndex >= 0 ? catalogAssets.selectedIndex : 0)
    }

    function selectedToneWheelName() {
        if (selectedToneWheel === 0)
            return "Shadows"
        if (selectedToneWheel === 2)
            return "Highlights"
        return "Midtones"
    }

    function selectedToneWheelHue() {
        if (selectedToneWheel === 0)
            return editorPreview.gradingShadowsHue
        if (selectedToneWheel === 2)
            return editorPreview.gradingHighlightsHue
        return editorPreview.gradingMidtonesHue
    }

    function selectedToneWheelSaturation() {
        if (selectedToneWheel === 0)
            return editorPreview.gradingShadowsSaturation
        if (selectedToneWheel === 2)
            return editorPreview.gradingHighlightsSaturation
        return editorPreview.gradingMidtonesSaturation
    }

    function selectedToneWheelLuminance() {
        if (selectedToneWheel === 0)
            return editorPreview.gradingShadowsLuminance
        if (selectedToneWheel === 2)
            return editorPreview.gradingHighlightsLuminance
        return editorPreview.gradingMidtonesLuminance
    }

    function setSelectedToneWheelHue(value) {
        if (selectedToneWheel === 0)
            editorPreview.gradingShadowsHue = value
        else if (selectedToneWheel === 2)
            editorPreview.gradingHighlightsHue = value
        else
            editorPreview.gradingMidtonesHue = value
    }

    function setSelectedToneWheelSaturation(value) {
        if (selectedToneWheel === 0)
            editorPreview.gradingShadowsSaturation = value
        else if (selectedToneWheel === 2)
            editorPreview.gradingHighlightsSaturation = value
        else
            editorPreview.gradingMidtonesSaturation = value
    }

    function setSelectedToneWheelLuminance(value) {
        if (selectedToneWheel === 0)
            editorPreview.gradingShadowsLuminance = value
        else if (selectedToneWheel === 2)
            editorPreview.gradingHighlightsLuminance = value
        else
            editorPreview.gradingMidtonesLuminance = value
    }

    function selectAsset(index) {
        if (!catalogAssets || index < 0 || index >= catalogAssets.assetCount)
            return
        if (selectionMode)
            catalogAssets.toggleSelectionIndex(index)
        else
            catalogAssets.selectIndex(index)
    }

    function canSelectAll() {
        return page === "library" && assetCount() > 0
    }

    function selectAll() {
        if (!canSelectAll())
            return
        selectionMode = true
        catalogAssets.selectAll()
    }

    function canDeselect() {
        return page === "library" && (selectionMode || catalogAssets.selectedAssetCount > 0)
    }

    function deselectAll() {
        if (!canDeselect())
            return
        catalogAssets.clearSelection()
        selectionMode = false
    }

    function queueExport(index) {
        if (!exportJobs || !catalogAssets || assetCount() === 0)
            return
        const asset = assetAt(index)
        const sourcePath = String(asset.filePath || catalogAssets.selectedFilePath || "")
        if (sourcePath.length === 0)
            return
        exportJobs.queueExport(
                    String(asset.assetId || sourcePath),
                    String(asset.mediaType || "photo"),
                    assetName(index),
                    sourcePath,
                    editorPreview.previewSource,
                    String(asset.recipeJson || catalogAssets.selectedRecipeJson || ""),
                    exportPresets[selectedPresetIndex].name,
                    exportDestination)
    }

    function startVaultImport() {
        root.importModeOpen = true
        catalogAssets.refreshImportSources()
    }

    function importFolderToVault() {
        root.importModeOpen = true
        catalogAssets.importFolder()
    }

    function activateEditTool(toolName) {
        root.editTool = toolName
        if (toolName === "adjust") {
            root.selectedEditLayer = "adjustments"
            root.editInspectorTab = "adjust"
            editorPreview.compareMode = false
            editorPreview.cropEditing = false
            return
        }
        if (toolName === "crop") {
            root.cropToolMode = "crop"
            editorPreview.compareMode = false
            editorPreview.cropEditing = true
            return
        }
        if (toolName === "masks") {
            editorPreview.compareMode = false
            editorPreview.cropEditing = false
            root.editInspectorTab = "adjust"
            return
        }
        if (toolName === "brush") {
            editorPreview.compareMode = false
            editorPreview.cropEditing = false
            editorPreview.startSelectionBrush(false)
            root.editInspectorTab = "adjust"
            return
        }
        if (toolName === "compare") {
            editorPreview.compareMode = true
            editorPreview.cropEditing = false
            return
        }
        if (toolName === "export") {
            root.assetActivated("export")
        }
    }

    function setCropToolMode(modeName) {
        root.editTool = "crop"
        root.selectedEditLayer = "crop"
        root.cropToolMode = modeName === "transform" ? "transform" : (modeName === "horizon" ? "horizon" : "crop")
        root.editInspectorTab = "detail"
        editorPreview.compareMode = false
        editorPreview.cropEditing = root.cropToolMode === "crop"
    }

    function applyHorizonStraightenGuide(startX, startY, endX, endY) {
        return editorPreview.straightenFromHorizon(startX, startY, endX, endY)
    }

    function cropPresetIsCustom(presetName) {
        return editorPreview.customCropAspectPresets().indexOf(String(presetName || "")) >= 0
    }

    function addCropAspectPreset(presetName) {
        const added = editorPreview.addCustomCropAspectPreset(presetName)
        if (!added)
            return false
        const customPresets = editorPreview.customCropAspectPresets()
        if (customPresets.length > 0)
            editorPreview.cropPreset = customPresets[customPresets.length - 1]
        return true
    }

    function removeCropAspectPreset(presetName) {
        return editorPreview.removeCustomCropAspectPreset(presetName)
    }

    function normalizedMaskCreationOperation(operationName) {
        const normalized = String(operationName || "").trim().toLowerCase()
        if (normalized === "add" || normalized === "subtract" || normalized === "intersect")
            return normalized
        return "add"
    }

    function maskKindLabel(kindName) {
        const kind = String(kindName || "").trim().toLowerCase()
        if (kind === "linear")
            return "Linear source"
        if (kind === "radial")
            return "Radial source"
        if (kind === "brush")
            return "Brush source"
        if (kind === "object")
            return "Object source"
        if (kind === "polygon")
            return "Polygon source"
        if (kind === "lasso")
            return "Lasso source"
        if (kind === "luminance")
            return "Luminance range"
        if (kind === "color")
            return "Color range"
        if (kind === "depth")
            return "Depth range"
        return "Mask source"
    }

    function maskOperationLabel(operationName) {
        const operation = String(operationName || "").trim().toLowerCase()
        if (operation === "subtract")
            return "Subtract"
        if (operation === "intersect")
            return "Intersect"
        if (operation === "exclude")
            return "Exclude"
        if (operation === "replace")
            return "Replace"
        if (operation === "union")
            return "Union"
        return "Add"
    }

    function activeMaskToolbarStatusText() {
        if (!editorPreview.hasActiveMask)
            return "No active mask set"
        const state = editorPreview.activeMaskInverted ? "Inverted" : "Normal"
        return root.maskKindLabel(editorPreview.activeMaskKind) + " / " +
               root.maskOperationLabel(editorPreview.activeMaskOperation) + " / " +
               state
    }

    function maskStackSourceSummary(maskSet) {
        const sourceCount = Number(maskSet.sourceCount || 0)
        if (sourceCount === 1)
            return "1 source"
        return sourceCount + " sources"
    }

    function setMaskCreationOperation(operationName) {
        root.maskCreationOperation = root.normalizedMaskCreationOperation(operationName)
        if (editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch")
            editorPreview.activeMaskOperation = root.maskCreationOperation
        return root.maskCreationOperation
    }

    function createMaskSource(kindName) {
        root.editTool = "masks"
        root.editInspectorTab = "adjust"
        editorPreview.compareMode = false
        editorPreview.cropEditing = false
        editorPreview.maskPreviewMode = "overlay"
        editorPreview.addMask(kindName)
        if (editorPreview.hasActiveMask)
            editorPreview.activeMaskOperation = root.maskCreationOperation
    }

    function startMaskBrushSource() {
        root.editTool = "brush"
        root.editInspectorTab = "adjust"
        editorPreview.compareMode = false
        editorPreview.cropEditing = false
        editorPreview.maskPreviewMode = "overlay"
        editorPreview.startSelectionBrush(root.maskCreationOperation === "subtract")
        if (editorPreview.hasActiveMask)
            editorPreview.activeMaskOperation = root.maskCreationOperation
    }

    function brushToolReady() {
        return page === "edit" &&
               editorPreview.hasActiveMask &&
               editorPreview.activeMaskKind === "brush" &&
               editorPreview.activeMaskToolRole !== "retouch"
    }

    function latestBrushPressureText() {
        const points = editorPreview.activeMaskLiveBrushPoints || []
        if (!editorPreview.brushStrokeActive || points.length === 0)
            return "Pressure --"
        const point = points[points.length - 1]
        const pressure = point && point.pressure !== undefined ? point.pressure : 1.0
        return "Pressure " + Math.round(root.clamp(pressure, 0.0, 1.0) * 100) + "%"
    }

    function brushHudStatusText() {
        return "Size " + Math.round(editorPreview.activeMaskBrushSize * 1000) +
               "  Flow " + Math.round(editorPreview.activeMaskBrushFlow * 100) + "%" +
               "  Hard " + Math.round(editorPreview.activeMaskBrushHardness * 100) + "%" +
               "  " + root.latestBrushPressureText()
    }

    function setBrushPaintMode(operationName) {
        const operation = root.normalizedMaskCreationOperation(operationName)
        root.maskCreationOperation = operation
        root.editTool = "brush"
        root.editInspectorTab = "adjust"
        editorPreview.compareMode = false
        editorPreview.cropEditing = false
        editorPreview.maskPreviewMode = "overlay"
        if (root.brushToolReady()) {
            editorPreview.activeMaskOperation = operation
            return operation
        }
        editorPreview.startSelectionBrush(operation === "subtract")
        if (editorPreview.hasActiveMask)
            editorPreview.activeMaskOperation = operation
        return operation
    }

    function adjustActiveBrushSize(delta) {
        if (!root.brushToolReady())
            return false
        editorPreview.activeMaskBrushSize = root.clamp(editorPreview.activeMaskBrushSize + delta, 0.01, 0.35)
        return true
    }

    function adjustActiveBrushFlow(delta) {
        if (!root.brushToolReady())
            return false
        editorPreview.activeMaskBrushFlow = root.clamp(editorPreview.activeMaskBrushFlow + delta, 0.05, 1.0)
        return true
    }

    function adjustActiveBrushHardness(delta) {
        if (!root.brushToolReady())
            return false
        editorPreview.activeMaskBrushHardness = root.clamp(editorPreview.activeMaskBrushHardness + delta, 0.0, 1.0)
        return true
    }

    function startRetouchTool(modeName) {
        const mode = String(modeName || "").trim().toLowerCase()
        root.editTool = "retouch"
        root.editInspectorTab = "adjust"
        editorPreview.compareMode = false
        editorPreview.cropEditing = false
        editorPreview.maskPreviewMode = "overlay"
        if (mode === "heal")
            editorPreview.startHealBrush()
        else if (mode === "patch")
            editorPreview.startPatchBrush()
        else if (mode === "clone")
            editorPreview.startCloneBrush()
        else if (mode === "dust")
            editorPreview.removeDustSpots()
        else if (mode === "red-eye")
            editorPreview.removeRedEye()
        else if (mode === "skin-cleanup" || mode === "skin")
            editorPreview.cleanupSkin()
        else if (mode === "frequency-smoothing" || mode === "smooth")
            editorPreview.smoothSkinFrequency()
        else
            editorPreview.startRemoveBrush()
        return editorPreview.activeMaskRetouchMode
    }

    function retouchModeLabel(modeName) {
        const mode = String(modeName || "").trim().toLowerCase()
        if (mode === "remove")
            return "Remove"
        if (mode === "heal")
            return "Heal"
        if (mode === "patch")
            return "Patch"
        if (mode === "clone")
            return "Clone Stamp"
        if (mode === "dust")
            return "Dust Cleanup"
        if (mode === "red-eye")
            return "Red Eye"
        if (mode === "skin-cleanup")
            return "Skin Cleanup"
        if (mode === "frequency-smoothing")
            return "Frequency Smoothing"
        return "Retouch"
    }

    function retouchRequirementText() {
        const mode = editorPreview.activeMaskRetouchMode
        if (mode === "clone")
            return editorPreview.activeMaskCloneSourceSet ? "Source set" : "Pick source"
        if (mode === "remove" || mode === "heal" || mode === "patch")
            return editorPreview.activeMaskCommitted ? "Applied" : "Paint area"
        if (mode === "dust" || mode === "red-eye" || mode === "skin-cleanup" ||
                mode === "frequency-smoothing")
            return editorPreview.activeMaskCommitted ? "Review applied" : "Review staged"
        return editorPreview.activeMaskCommitted ? "Applied" : "Staged"
    }

    function retouchHudStatusText() {
        if (!editorPreview.hasActiveMask || editorPreview.activeMaskToolRole !== "retouch")
            return "Retouch"
        return root.retouchModeLabel(editorPreview.activeMaskRetouchMode) + " / " +
               root.retouchRequirementText() + " / " +
               (editorPreview.retouchBeforeAfterEnabled ? "Before" : "After")
    }

    function photoLayerBlendModes() {
        return ["normal", "multiply", "screen", "overlay", "soft-light", "hard-light", "color", "luminosity"]
    }

    function photoLayerBlendLabel(modeName) {
        const mode = String(modeName || "normal").trim().toLowerCase()
        if (mode === "soft-light")
            return "Soft Light"
        if (mode === "hard-light")
            return "Hard Light"
        return mode.length > 0 ? mode.charAt(0).toUpperCase() + mode.slice(1) : "Normal"
    }

    function nextPhotoLayerBlendMode(modeName) {
        const modes = root.photoLayerBlendModes()
        const current = String(modeName || "normal").trim().toLowerCase()
        const index = modes.indexOf(current)
        return modes[(index + 1) % modes.length]
    }

    function cyclePhotoLayerBlendMode(layerIndex, modeName) {
        return editorPreview.setPhotoLayerBlendMode(layerIndex, root.nextPhotoLayerBlendMode(modeName))
    }

    function adjustPhotoLayerOpacity(layerIndex, opacity, delta) {
        const currentOpacity = opacity === undefined || opacity === null ? 1.0 : Number(opacity)
        const nextOpacity = Math.round(root.clamp(currentOpacity + delta, 0.0, 1.0) * 100) / 100
        return editorPreview.setPhotoLayerOpacity(layerIndex, nextOpacity)
    }

    function normalizedActivePhotoLayerIndex() {
        if (editorPreview.photoLayerCount <= 0)
            return -1
        return Math.max(0, Math.min(editorPreview.photoLayerCount - 1, root.activePhotoLayerIndex))
    }

    function currentPhotoLayerData() {
        const index = root.normalizedActivePhotoLayerIndex()
        if (index < 0)
            return ({})
        return editorPreview.photoLayers[index] || ({})
    }

    function photoLayerHasDirectTransform(layer) {
        const kind = String(layer.kind || "").trim().toLowerCase()
        return (kind === "pixel" || kind === "text" || kind === "source") &&
               Number(layer.pixelWidth || 0) > 0 &&
               Number(layer.pixelHeight || 0) > 0
    }

    function sourceCanvasWidthForLayers(layer) {
        const sourceWidth = Number(editorPreview.sourceImageWidth || 0)
        if (sourceWidth > 0)
            return sourceWidth
        return Math.max(1, Number(layer.x || 0) + Number(layer.pixelWidth || 0) * Number(layer.scale || 1))
    }

    function sourceCanvasHeightForLayers(layer) {
        const sourceHeight = Number(editorPreview.sourceImageHeight || 0)
        if (sourceHeight > 0)
            return sourceHeight
        return Math.max(1, Number(layer.y || 0) + Number(layer.pixelHeight || 0) * Number(layer.scale || 1))
    }

    function setActivePhotoLayer(index) {
        if (index < 0 || index >= editorPreview.photoLayerCount)
            return false
        root.activePhotoLayerIndex = index
        root.editTool = "adjustments"
        root.editInspectorTab = "adjust"
        return true
    }

    function selectEditStack(index) {
        const keys = ["adjustments", "color", "hsl", "curve", "detail", "lens", "crop"]
        root.selectedEditLayer = keys[Math.max(0, Math.min(keys.length - 1, index))]
        root.editTool = root.selectedEditLayer
        root.editInspectorTab = root.selectedEditLayer === "detail" ? "detail" : "adjust"
        if (index === 2)
            editorPreview.hslChannel = "Blues"
        else if (index === 3)
            editorPreview.curveChannel = "RGB"
        else if (index === 6) {
            root.cropToolMode = "crop"
            editorPreview.cropEditing = true
        } else {
            editorPreview.cropEditing = false
        }
    }

    Shortcut {
        objectName: "brushAddShortcut"
        sequence: "B"
        context: Qt.WindowShortcut
        enabled: page === "edit" && editorPreview.hasAsset && root.editTool !== "compare"
        onActivated: root.setBrushPaintMode("add")
    }

    Shortcut {
        objectName: "brushEraseShortcut"
        sequence: "E"
        context: Qt.WindowShortcut
        enabled: page === "edit" && editorPreview.hasAsset && root.editTool !== "compare"
        onActivated: root.setBrushPaintMode("subtract")
    }

    Shortcut {
        objectName: "brushSizeDownShortcut"
        sequence: "["
        context: Qt.WindowShortcut
        enabled: root.brushToolReady()
        onActivated: root.adjustActiveBrushSize(-0.02)
    }

    Shortcut {
        objectName: "brushSizeUpShortcut"
        sequence: "]"
        context: Qt.WindowShortcut
        enabled: root.brushToolReady()
        onActivated: root.adjustActiveBrushSize(0.02)
    }

    Shortcut {
        objectName: "brushHardnessDownShortcut"
        sequence: "Shift+["
        context: Qt.WindowShortcut
        enabled: root.brushToolReady()
        onActivated: root.adjustActiveBrushHardness(-0.08)
    }

    Shortcut {
        objectName: "brushHardnessUpShortcut"
        sequence: "Shift+]"
        context: Qt.WindowShortcut
        enabled: root.brushToolReady()
        onActivated: root.adjustActiveBrushHardness(0.08)
    }

    component Panel: Rectangle {
        id: panel
        default property alias content: panelContent.data
        property string title: ""
        property string subhead: ""
        property bool headerVisible: title.length > 0
        property bool framed: true
        color: root.panelColor
        border.width: theme.chromeBorderWidth
        border.color: root.hairline
        radius: 3
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                visible: panel.headerVisible
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: panel.title
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0
                    elide: Text.ElideRight
                }

                Rectangle {
                    visible: panel.subhead.length > 0
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    color: root.mint
                    radius: 4
                }
            }

            Item {
                id: panelContent
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    component RowRule: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 0
        color: "transparent"
    }

    component SectionTitle: Label {
        property string label: ""
        text: label
        color: theme.textStrong
        font.family: theme.fontUi
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0
    }

    component MiniButton: Button {
        id: mini
        property bool active: false
        property color accent: root.mint
        property string iconName: ""
        property string tooltip: ""
        property bool labelVisible: true
        property bool square: false
        property string role: square ? "icon" : "secondary"
        readonly property bool destructive: role === "destructive"
        readonly property bool ghost: role === "ghost"
        readonly property bool segmented: role === "segmented"
        readonly property bool iconOnly: role === "icon" || square
        readonly property bool visualIconOnly: iconOnly || (!labelVisible && iconName.length > 0)
        readonly property bool commandShape: !iconOnly && !segmented && !ghost
        readonly property bool slashEdge: commandShape
        readonly property int commandCut: commandShape ? 12 : 0
        implicitHeight: 30
        implicitWidth: visualIconOnly ? 30 : Math.max(62, label.implicitWidth + (iconName.length > 0 ? 32 : 20))
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        ToolTip.visible: hovered && tooltip.length > 0
        ToolTip.delay: 300
        ToolTip.timeout: 4200
        ToolTip.text: tooltip

        background: Item {
            anchors.fill: parent

            CommandButtonSurface {
                anchors.fill: parent
                visible: !mini.ghost
                cut: mini.commandCut
                borderWidth: mini.segmented && !mini.active ? 0 : 1
                fillColor: {
                    if (!mini.enabled)
                        return "#141719"
                    if (mini.role === "primary")
                        return theme.alpha(mini.accent, mini.down ? 0.22 : (mini.hovered || mini.active ? 0.16 : 0.08))
                    if (mini.destructive)
                        return mini.hovered || mini.active ? theme.alpha(theme.error, 0.12) : "#0B0F11"
                    if (mini.active)
                        return mini.iconOnly ? theme.alpha(mini.accent, 0.14) : theme.alpha(mini.accent, mini.segmented ? 0.08 : 0.11)
                    if (mini.down)
                        return "#171D20"
                    return mini.hovered ? "#12191C" : "#0B0F11"
                }
                borderColor: {
                    if (!mini.enabled)
                        return theme.alpha(root.lineColor, 0.32)
                    if (mini.destructive)
                        return mini.hovered || mini.active ? theme.error : theme.alpha(theme.error, 0.46)
                    if (mini.role === "primary")
                        return mini.active ? mini.accent : theme.alpha(mini.accent, mini.hovered ? 0.78 : 0.56)
                    if (mini.active)
                        return mini.accent
                    return mini.hovered ? theme.alpha(mini.accent, 0.52) : root.hairline
                }
                borderOpacity: 1
                accentColor: mini.destructive ? theme.error : mini.accent
                accentWidth: mini.role === "primary" || mini.destructive ? 5 : 4
                accentOpacity: !mini.enabled || !mini.slashEdge ? 0 : (mini.role === "primary" || mini.destructive || mini.active ? 1.0 : (mini.hovered ? 0.82 : 0.58))
                topLineOpacity: mini.slashEdge && mini.enabled ? (mini.hovered || mini.active ? 0.28 : 0.12) : 0
            }

            Rectangle {
                visible: mini.segmented && mini.active
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2
                color: mini.accent
            }

            Rectangle {
                visible: mini.ghost && (mini.hovered || mini.active)
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: mini.destructive ? theme.error : mini.accent
            }

            CommandButtonSurface {
                visible: mini.active && !mini.ghost
                anchors.fill: parent
                cut: mini.commandCut
                fillColor: "transparent"
                borderWidth: 1
                borderColor: theme.alpha(mini.accent, 0.38)
                accentColor: mini.destructive ? theme.error : mini.accent
                accentWidth: mini.role === "primary" || mini.destructive ? 5 : 4
                accentOpacity: mini.slashEdge && mini.enabled ? 1.0 : 0
                topLineOpacity: 0
            }
        }

        contentItem: RowLayout {
            anchors.centerIn: parent
            spacing: 8

            AppIcon {
                visible: mini.iconName.length > 0
                name: mini.iconName
                color: mini.destructive ? theme.error : (mini.role === "primary" && mini.active ? "#EFFFFB" : (mini.active ? mini.accent : theme.textMain))
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
            }

            Label {
                id: label
                visible: !mini.visualIconOnly
                text: mini.text
                color: mini.destructive ? theme.error : (mini.role === "primary" && mini.active ? "#EFFFFB" : (mini.active ? theme.textStrong : theme.textMain))
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: mini.active || mini.role === "primary" ? Font.DemiBold : Font.Medium
                font.letterSpacing: 0
            }
        }
    }

    component TinyInput: Rectangle {
        id: field
        property alias text: input.text
        property string placeholder: ""
        Layout.preferredHeight: 34
        color: "#0A0D0F"
        border.width: theme.chromeBorderWidth
        border.color: root.hairline
        radius: 3

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            verticalAlignment: TextInput.AlignVCenter
            color: theme.textMain
            selectionColor: root.mint
            selectedTextColor: "#06100E"
            font.family: theme.fontUi
            font.pixelSize: 12
            clip: true
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            visible: input.text.length === 0
            text: field.placeholder
            color: root.dimText
            font.family: theme.fontUi
            font.pixelSize: 12
        }
    }

    component ThumbnailCard: Rectangle {
        id: card
        property int assetIndex: 0
        property bool selected: catalogAssets && catalogAssets.selectedIndex === assetIndex
        property string badge: "RAW"
        property bool filmstrip: false
        signal activated()
        signal doubleActivated()

        radius: 4
        color: filmstrip ? (selected ? "#12181B" : "#0B0F11") : (selected ? "#182124" : "#111719")
        border.width: theme.chromeBorderWidth
        border.color: filmstrip ? (selected ? "#C8D0D6" : root.hairline) : (selected ? root.mint : root.hairline)
        clip: true

        Image {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height - 68
            source: root.assetThumb(card.assetIndex)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: root.assetThumbCacheable(card.assetIndex)
            visible: !card.filmstrip && source.toString().length > 0
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height - 68
            color: "#172026"
            visible: !card.filmstrip && root.assetThumb(card.assetIndex).length === 0
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 68
            color: "#0E1315"
            border.width: 0
            visible: !card.filmstrip

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: root.assetName(card.assetIndex)
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    Label {
                        text: card.badge
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        padding: 3
                        background: Rectangle {
                            color: "#172024"
                            border.width: theme.chromeBorderWidth
                            border.color: root.hairlineStrong
                            radius: 2
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Oct 1, 2025  -  DJI \"ZH20T\""
                    color: root.dimText
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }

                Row {
                    spacing: 2
                    Repeater {
                        model: 5
                        Label {
                            required property int index
                            text: "*"
                            color: index < 1 ? root.mint : root.faintText
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 0
            visible: card.filmstrip

            Rectangle {
                id: filmstripImageFrame
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 3
                color: "#050708"
                border.width: theme.chromeBorderWidth
                border.color: card.selected ? "#C8D0D6" : root.hairline
                clip: true

                Image {
                    id: filmstripImage
                    anchors.fill: parent
                    anchors.margins: 5
                    source: root.assetThumb(card.assetIndex)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: root.assetThumbCacheable(card.assetIndex)
                    visible: source.toString().length > 0
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 5
                    color: "#172026"
                    visible: filmstripImage.source.toString().length === 0
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 5
                    width: 16
                    height: 16
                    radius: 2
                    visible: root.selectionMode || card.selected
                    color: card.selected ? "#C8D0D6" : theme.alpha(theme.ink950, 0.72)
                    border.width: theme.chromeBorderWidth
                    border.color: card.selected ? "#EEF2F4" : root.hairlineStrong

                    AppIcon {
                        anchors.centerIn: parent
                        width: 10
                        height: 10
                        name: "check"
                        color: "#07110F"
                        visible: card.selected
                    }
                }

                Label {
                    visible: filmstripImage.status === Image.Ready && root.aspectRatioText(filmstripImage.sourceSize.width, filmstripImage.sourceSize.height).length > 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 5
                    text: root.aspectRatioText(filmstripImage.sourceSize.width, filmstripImage.sourceSize.height)
                    color: "#C8D0D6"
                    font.family: theme.fontMono
                    font.pixelSize: 8
                    padding: 3
                    background: Rectangle {
                        color: theme.alpha(theme.ink950, 0.74)
                        border.width: theme.chromeBorderWidth
                        border.color: root.hairlineStrong
                        radius: 2
                    }
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            width: 18
            height: 18
            radius: 2
            visible: !card.filmstrip && (root.selectionMode || card.selected)
            color: card.selected ? root.mint : theme.alpha(theme.ink950, 0.72)
            border.width: theme.chromeBorderWidth
            border.color: card.selected ? root.mintSoft : root.hairlineStrong

            AppIcon {
                anchors.centerIn: parent
                width: 11
                height: 11
                name: "check"
                color: "#07110F"
                visible: card.selected
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: card.activated()
            onDoubleClicked: card.doubleActivated()
        }
    }

    component ImageViewport: Rectangle {
        id: viewport
        property string source: root.displayImageSource(0)
        property bool masked: false
        property bool framed: true
        property string resetKey: source
        property real zoom: 1.0
        property real panX: 0
        property real panY: 0
        property string fitMode: "fit"
        property bool syncViewport: false
        property bool loupeEnabled: false
        property bool focusCheckOverlayEnabled: false
        property bool focusPeakingOverlayEnabled: false
        property bool clippingOverlayEnabled: false
        property bool falseColorOverlayEnabled: false
        property bool gamutOverlayEnabled: false
        property bool zoneMapOverlayEnabled: false
        property bool applyingSynchronizedView: false
        property real pointerX: width / 2
        property real pointerY: height / 2
        readonly property real loupeSize: 168
        readonly property int contentMargin: framed ? 10 : 0
        readonly property real paintedContentX: contentMargin
            + ((width - contentMargin * 2) - previewImage.paintedWidth) / 2
            + panX - previewImage.paintedWidth * (zoom - 1) / 2
        readonly property real paintedContentY: contentMargin
            + ((height - contentMargin * 2) - previewImage.paintedHeight) / 2
            + panY - previewImage.paintedHeight * (zoom - 1) / 2
        readonly property real paintedContentWidth: previewImage.paintedWidth * zoom
        readonly property real paintedContentHeight: previewImage.paintedHeight * zoom
        color: "#050708"
        border.width: theme.chromeBorderWidth
        border.color: root.hairline
        radius: 4
        clip: true

        function setZoom(nextZoom) {
            zoom = root.clamp(nextZoom, 0.25, 4.0)
            if (zoom <= 1.0) {
                panX = 0
                panY = 0
            }
        }

        function publishSynchronizedView() {
            if (!syncViewport || !root.syncEnabled || applyingSynchronizedView)
                return
            root.synchronizedZoom = zoom
            root.synchronizedPanX = panX
            root.synchronizedPanY = panY
        }

        function applySynchronizedView() {
            if (!syncViewport || !root.syncEnabled || applyingSynchronizedView)
                return
            applyingSynchronizedView = true
            zoom = root.synchronizedZoom
            panX = root.synchronizedPanX
            panY = root.synchronizedPanY
            applyingSynchronizedView = false
        }

        function resetView() {
            fitMode = "fit"
            zoom = 1.0
            panX = 0
            panY = 0
        }

        function zoomIn() {
            setZoom(zoom >= 1.0 ? zoom + 0.25 : 1.0)
        }

        function zoomOut() {
            setZoom(zoom <= 1.0 ? 0.75 : zoom - 0.25)
        }

        function normalizedPointForViewport(x, y) {
            if (paintedContentWidth <= 1 || paintedContentHeight <= 1)
                return { valid: false, x: 0, y: 0 }
            const normalizedX = (x - paintedContentX) / Math.max(1, paintedContentWidth)
            const normalizedY = (y - paintedContentY) / Math.max(1, paintedContentHeight)
            return {
                valid: normalizedX >= 0 && normalizedX <= 1 && normalizedY >= 0 && normalizedY <= 1,
                x: root.clamp(normalizedX, 0, 1),
                y: root.clamp(normalizedY, 0, 1)
            }
        }

        function updatePixelInspectorAt(x, y) {
            const point = normalizedPointForViewport(x, y)
            if (point.valid)
                editorPreview.updatePixelInspector(point.x, point.y)
            else
                editorPreview.clearPixelInspector()
        }

        onResetKeyChanged: resetView()
        onSyncViewportChanged: applySynchronizedView()
        onZoomChanged: publishSynchronizedView()
        onPanXChanged: publishSynchronizedView()
        onPanYChanged: publishSynchronizedView()

        Connections {
            target: root
            function onSynchronizedZoomChanged() { viewport.applySynchronizedView() }
            function onSynchronizedPanXChanged() { viewport.applySynchronizedView() }
            function onSynchronizedPanYChanged() { viewport.applySynchronizedView() }
            function onSyncEnabledChanged() { viewport.applySynchronizedView() }
        }

        Image {
            id: previewImage
            anchors.fill: parent
            anchors.margins: viewport.contentMargin
            source: viewport.source
            fillMode: viewport.fitMode === "fill" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
            asynchronous: true
            cache: false
            visible: source.toString().length > 0
            scale: viewport.zoom
            transform: Translate {
                x: viewport.panX
                y: viewport.panY
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "#11181D"
            visible: previewImage.source.toString().length === 0
        }

        Canvas {
            id: focusCheckOverlay
            objectName: "focusCheckOverlay"
            anchors.fill: previewImage
            visible: root.focusCheckOverlayEnabled && viewport.focusCheckOverlayEnabled && previewImage.visible
            opacity: 0.72
            z: 11
            onVisibleChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = theme.alpha(root.mint, 0.88)
                ctx.lineWidth = 1
                const step = 18
                for (let y = step; y < height; y += step) {
                    ctx.beginPath()
                    ctx.moveTo(0, y)
                    ctx.lineTo(width, y)
                    ctx.stroke()
                }
                ctx.strokeStyle = theme.alpha(theme.accentWarm400, 0.72)
                for (let x = step * 0.5; x < width; x += step) {
                    ctx.beginPath()
                    ctx.moveTo(x, 0)
                    ctx.lineTo(x, height)
                    ctx.stroke()
                }
            }
        }

        Canvas {
            id: clippingOverlay
            objectName: "clippingOverlay"
            anchors.fill: previewImage
            visible: root.clippingOverlayEnabled && viewport.clippingOverlayEnabled && previewImage.visible
            opacity: 0.82
            z: 12
            onVisibleChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            Connections {
                target: editorPreview
                function onHistogramChanged() { clippingOverlay.requestPaint() }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)

                const sampleCount = Math.max(1, editorPreview.histogramSampleCount)
                const shadowRatio = editorPreview.histogramShadowClipCount / sampleCount
                const highlightRatio = editorPreview.histogramHighlightClipCount / sampleCount
                const shadowBand = Math.max(10, Math.min(height * 0.35, height * (0.12 + shadowRatio * 8)))
                const highlightBand = Math.max(10, Math.min(height * 0.35, height * (0.12 + highlightRatio * 8)))

                ctx.fillStyle = "rgba(0, 92, 255, 0.26)"
                ctx.fillRect(0, height - shadowBand, width, shadowBand)
                ctx.strokeStyle = "rgba(64, 160, 255, 0.78)"
                ctx.lineWidth = 1
                for (let x = -height; x < width; x += 16) {
                    ctx.beginPath()
                    ctx.moveTo(x, height)
                    ctx.lineTo(x + shadowBand, height - shadowBand)
                    ctx.stroke()
                }

                ctx.fillStyle = "rgba(255, 54, 72, 0.24)"
                ctx.fillRect(0, 0, width, highlightBand)
                ctx.strokeStyle = "rgba(255, 104, 112, 0.82)"
                for (let x = 0; x < width + highlightBand; x += 16) {
                    ctx.beginPath()
                    ctx.moveTo(x, 0)
                    ctx.lineTo(x - highlightBand, highlightBand)
                    ctx.stroke()
                }
            }
        }

        Label {
            id: clippingOverlayTargetLabel
            objectName: "clippingOverlayTargetLabel"
            anchors.left: previewImage.left
            anchors.top: previewImage.top
            anchors.margins: 10
            visible: clippingOverlay.visible
            z: 17
            text: "Preview clipping  Blue shadows / Red highlights"
            color: "#f4f7fa"
            font.family: theme.fontMono
            font.pixelSize: 10
            padding: 5
            background: Rectangle {
                color: theme.alpha(theme.ink950, 0.82)
                border.width: theme.chromeBorderWidth
                border.color: theme.alpha("#ff6870", 0.45)
                radius: 3
            }
        }

        Canvas {
            id: focusPeakingOverlay
            objectName: "focusPeakingOverlay"
            anchors.fill: previewImage
            visible: root.focusPeakingOverlayEnabled && viewport.focusPeakingOverlayEnabled && previewImage.visible
            opacity: 0.84
            z: 13
            onVisibleChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)

                const step = 14
                const radius = 1.35
                for (let y = step * 0.5; y < height; y += step) {
                    const phase = Math.floor(y / step) % 2 === 0 ? 0 : step * 0.5
                    for (let x = phase; x < width; x += step) {
                        const edgeStrength = (Math.sin(x * 0.13) + Math.cos(y * 0.17) + 2) / 4
                        if (edgeStrength < 0.42)
                            continue
                        ctx.fillStyle = edgeStrength > 0.72
                            ? "rgba(255, 70, 96, 0.92)"
                            : "rgba(25, 224, 176, 0.82)"
                        ctx.beginPath()
                        ctx.arc(x, y, radius + edgeStrength * 1.4, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }

                ctx.strokeStyle = "rgba(255, 255, 255, 0.34)"
                ctx.lineWidth = 1
                for (let x = -height; x < width; x += 36) {
                    ctx.beginPath()
                    ctx.moveTo(x, height)
                    ctx.lineTo(x + height, 0)
                    ctx.stroke()
                }
            }
        }

        Canvas {
            id: falseColorOverlay
            objectName: "falseColorOverlay"
            anchors.fill: previewImage
            visible: root.falseColorOverlayEnabled && viewport.falseColorOverlayEnabled && previewImage.visible
            opacity: 0.58
            z: 14
            onVisibleChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                const bands = [
                    "#1016a8", "#004dff", "#00a6ff", "#00d084",
                    "#f7e85b", "#ff9f1a", "#ff2e2e", "#ffffff"
                ]
                const bandHeight = Math.max(1, height / bands.length)
                for (let i = 0; i < bands.length; ++i) {
                    ctx.fillStyle = bands[i]
                    ctx.fillRect(0, height - (i + 1) * bandHeight, width, bandHeight + 1)
                }
                ctx.globalCompositeOperation = "destination-in"
                const gradient = ctx.createLinearGradient(0, 0, width, height)
                gradient.addColorStop(0, "rgba(255,255,255,0.62)")
                gradient.addColorStop(0.5, "rgba(255,255,255,0.34)")
                gradient.addColorStop(1, "rgba(255,255,255,0.68)")
                ctx.fillStyle = gradient
                ctx.fillRect(0, 0, width, height)
                ctx.globalCompositeOperation = "source-over"
            }
        }

        Canvas {
            id: gamutOverlay
            objectName: "gamutOverlay"
            anchors.fill: previewImage
            visible: root.gamutOverlayEnabled && viewport.gamutOverlayEnabled && previewImage.visible
            opacity: 0.66
            z: 15
            onVisibleChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                ctx.lineWidth = 1
                ctx.strokeStyle = "rgba(255, 72, 112, 0.74)"
                ctx.fillStyle = "rgba(255, 72, 112, 0.14)"
                const step = 22
                for (let y = 0; y < height; y += step) {
                    for (let x = (Math.floor(y / step) % 2) * step * 0.5; x < width; x += step) {
                        const risk = (Math.sin(x * 0.07) + Math.cos(y * 0.11) + 2) / 4
                        if (risk < 0.62)
                            continue
                        ctx.beginPath()
                        ctx.rect(x, y, step * 0.72, step * 0.72)
                        ctx.fill()
                        ctx.stroke()
                    }
                }
            }
        }

        Label {
            objectName: "gamutOverlayTargetLabel"
            anchors.left: previewImage.left
            anchors.top: clippingOverlayTargetLabel.visible ? clippingOverlayTargetLabel.bottom : previewImage.top
            anchors.leftMargin: 10
            anchors.topMargin: clippingOverlayTargetLabel.visible ? 6 : 10
            visible: gamutOverlay.visible
            z: 17
            text: "Output gamut  " + editorPreview.outputColorSpace
            color: "#f4f7fa"
            font.family: theme.fontMono
            font.pixelSize: 10
            padding: 5
            background: Rectangle {
                color: theme.alpha(theme.ink950, 0.82)
                border.width: theme.chromeBorderWidth
                border.color: theme.alpha("#ff4870", 0.45)
                radius: 3
            }
        }

        Canvas {
            id: zoneMapOverlay
            objectName: "zoneMapOverlay"
            anchors.fill: previewImage
            visible: root.zoneMapOverlayEnabled && viewport.zoneMapOverlayEnabled && previewImage.visible
            opacity: 0.62
            z: 16
            onVisibleChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                const zones = 11
                const bandWidth = Math.max(1, width / zones)
                for (let i = 0; i < zones; ++i) {
                    const value = i / (zones - 1)
                    ctx.fillStyle = Qt.rgba(value, value, value, 0.34)
                    ctx.fillRect(i * bandWidth, 0, bandWidth + 1, height)
                    ctx.fillStyle = value > 0.48 ? "rgba(0,0,0,0.58)" : "rgba(255,255,255,0.72)"
                    ctx.font = "10px sans-serif"
                    ctx.fillText(String(i), i * bandWidth + 5, 16)
                }
            }
        }

        Rectangle {
            id: loupeView
            objectName: "loupeView"
            visible: root.loupeEnabled && viewport.loupeEnabled && previewImage.visible
            x: root.clamp(root.loupeX - width / 2, 6, Math.max(6, viewport.width - width - 6))
            y: root.clamp(root.loupeY - height / 2, 6, Math.max(6, viewport.height - height - 6))
            width: Math.min(viewport.loupeSize, Math.max(96, viewport.width - 12))
            height: width
            radius: width / 2
            color: "#050708"
            border.width: 2
            border.color: theme.alpha(root.mint, 0.88)
            clip: true
            z: 12

            Image {
                width: previewImage.width * root.loupeZoom
                height: previewImage.height * root.loupeZoom
                x: loupeView.width / 2 - (root.loupeX - previewImage.x) * root.loupeZoom
                y: loupeView.height / 2 - (root.loupeY - previewImage.y) * root.loupeZoom
                source: viewport.source
                fillMode: previewImage.fillMode
                asynchronous: true
                cache: false
                scale: viewport.zoom
                transform: Translate {
                    x: viewport.panX * root.loupeZoom
                    y: viewport.panY * root.loupeZoom
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 1
                color: theme.alpha(theme.polar, 0.78)
            }

            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: 16
                color: theme.alpha(theme.polar, 0.78)
            }
        }

        Rectangle {
            objectName: viewport.objectName.length > 0 ? viewport.objectName + "GenericMaskTint" : "genericMaskTint"
            readonly property real targetOpacity: 0.18
            anchors.fill: previewImage
            visible: viewport.masked && !editorPreview.hasActiveMask
            color: "#24DCC2"
            opacity: targetOpacity
        }

        Item {
            objectName: viewport.objectName.length > 0 ? viewport.objectName + "MaskCanvasOverlay" : "maskCanvasOverlay"
            x: viewport.paintedContentWidth > 1 ? viewport.paintedContentX : viewport.contentMargin
            y: viewport.paintedContentHeight > 1 ? viewport.paintedContentY : viewport.contentMargin
            width: viewport.paintedContentWidth > 1 ? viewport.paintedContentWidth : Math.max(1, viewport.width - viewport.contentMargin * 2)
            height: viewport.paintedContentHeight > 1 ? viewport.paintedContentHeight : Math.max(1, viewport.height - viewport.contentMargin * 2)
            readonly property string previewSource: String(editorPreview.activeMaskPreviewSource || "")
            readonly property real previewOverlayOpacity: 0.42
            readonly property real selectionOverlayOpacity: 0.64
            readonly property real geometricOverlayOpacity: 0.52
            readonly property real geometricFillOpacity: 0.10
            readonly property real geometricStrokeOpacity: 0.74
            visible: viewport.masked &&
                     editorPreview.hasActiveMask &&
                     editorPreview.maskPreviewMode !== "marching-ants" &&
                     !editorPreview.compareMode
            z: 17

            Image {
                anchors.fill: parent
                visible: parent.previewSource.length > 0
                source: parent.previewSource
                fillMode: Image.Stretch
                smooth: true
                mipmap: false
                cache: false
                opacity: editorPreview.maskPreviewMode === "overlay" ? parent.previewOverlayOpacity : parent.selectionOverlayOpacity
            }

            Canvas {
                id: geometricMaskCanvas
                objectName: viewport.objectName.length > 0 ? viewport.objectName + "GeometricMaskCanvas" : "geometricMaskCanvas"
                anchors.fill: parent
                visible: parent.previewSource.length === 0
                opacity: parent.geometricOverlayOpacity
                onVisibleChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                Connections {
                    target: editorPreview
                    function onControlsChanged() { geometricMaskCanvas.requestPaint() }
                    function onMaskOverlayChanged() { geometricMaskCanvas.requestPaint() }
                }

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.clearRect(0, 0, width, height)
                    ctx.lineWidth = 2
                    ctx.strokeStyle = theme.alpha(root.mint, parent.geometricStrokeOpacity)
                    ctx.fillStyle = theme.alpha(root.mint, parent.geometricFillOpacity)
                    if (editorPreview.activeMaskKind === "radial") {
                        const cx = width * editorPreview.activeMaskCenterX
                        const cy = height * editorPreview.activeMaskCenterY
                        const rx = Math.max(12, width * editorPreview.activeMaskRadiusX)
                        const ry = Math.max(12, height * editorPreview.activeMaskRadiusY)
                        ctx.beginPath()
                        ctx.ellipse(cx, cy, rx, ry, editorPreview.activeMaskRotation * Math.PI / 180.0, 0, Math.PI * 2)
                        ctx.fill()
                        ctx.stroke()
                    } else if (editorPreview.activeMaskKind === "linear") {
                        const position = height * editorPreview.activeMaskPosition
                        const feather = Math.max(18, height * editorPreview.activeMaskFeather)
                        ctx.fillRect(0, root.clamp(position - feather / 2, 0, height), width, Math.min(feather, height))
                        ctx.beginPath()
                        ctx.moveTo(0, position)
                        ctx.lineTo(width, position)
                        ctx.stroke()
                    } else {
                        ctx.strokeRect(1, 1, Math.max(0, width - 2), Math.max(0, height - 2))
                    }
                }
            }
        }

        Rectangle {
            id: brushCursorRing
            objectName: viewport.objectName.length > 0 ? viewport.objectName + "BrushCursorRing" : "brushCursorRing"
            readonly property real brushDiameter: Math.max(
                8,
                Math.min(viewport.paintedContentWidth, viewport.paintedContentHeight) *
                    editorPreview.activeMaskBrushSize * 2)
            readonly property real hardnessDiameter: Math.max(2, brushDiameter * editorPreview.activeMaskBrushHardness)
            readonly property string brushModeLabel: editorPreview.activeMaskOperation === "subtract" ? "Erase" : "Add"
            readonly property string pressureText: root.latestBrushPressureText()
            visible: root.brushToolReady() && viewportMouse.containsMouse && previewImage.visible
            x: root.clamp(viewport.pointerX - width / 2,
                          viewport.paintedContentX - width / 2,
                          viewport.paintedContentX + viewport.paintedContentWidth - width / 2)
            y: root.clamp(viewport.pointerY - height / 2,
                          viewport.paintedContentY - height / 2,
                          viewport.paintedContentY + viewport.paintedContentHeight - height / 2)
            width: brushDiameter
            height: brushDiameter
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: editorPreview.activeMaskOperation === "subtract" ? "#d98962" : root.mint
            z: 22

            Rectangle {
                objectName: viewport.objectName.length > 0 ? viewport.objectName + "BrushCursorHardnessRing" : "brushCursorHardnessRing"
                anchors.centerIn: parent
                width: brushCursorRing.hardnessDiameter
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.62)
            }

            Label {
                objectName: viewport.objectName.length > 0 ? viewport.objectName + "BrushCursorModeLabel" : "brushCursorModeLabel"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: 5
                text: brushCursorRing.brushModeLabel + " " +
                      Math.round(editorPreview.activeMaskBrushSize * 1000) +
                      " / Hard " + Math.round(editorPreview.activeMaskBrushHardness * 100) + "%"
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 10
                padding: 4
                background: Rectangle {
                    color: theme.alpha(theme.ink950, 0.78)
                    border.width: theme.chromeBorderWidth
                    border.color: theme.alpha(theme.polar, 0.28)
                    radius: 2
                }
            }
        }

        Item {
            id: cloneSourcePin
            objectName: viewport.objectName.length > 0 ? viewport.objectName + "CloneSourcePin" : "cloneSourcePin"
            property real sourceX: editorPreview.activeMaskCloneSourceX
            property real sourceY: editorPreview.activeMaskCloneSourceY
            property string sourceStatus: editorPreview.activeMaskCloneSourceSet ? "Source set" : "Pick source"
            visible: root.page === "edit" &&
                     editorPreview.hasActiveMask &&
                     editorPreview.activeMaskToolRole === "retouch" &&
                     editorPreview.activeMaskRetouchMode === "clone" &&
                     editorPreview.activeMaskCloneSourceSet
            x: viewport.paintedContentX + sourceX * viewport.paintedContentWidth - width / 2
            y: viewport.paintedContentY + sourceY * viewport.paintedContentHeight - height / 2
            width: 34
            height: 34
            z: 23

            function moveSource(normalizedX, normalizedY) {
                editorPreview.setActiveMaskCloneSource(
                    root.clamp(normalizedX, 0.0, 1.0),
                    root.clamp(normalizedY, 0.0, 1.0))
            }

            Rectangle {
                anchors.centerIn: parent
                width: 24
                height: 24
                radius: 12
                color: theme.alpha("#7cb5ff", clonePinMouse.containsMouse || clonePinMouse.pressed ? 0.18 : 0.08)
                border.width: 2
                border.color: "#7cb5ff"
            }

            Rectangle {
                anchors.centerIn: parent
                width: 2
                height: 32
                radius: 1
                color: "#f4f7fa"
            }

            Rectangle {
                anchors.centerIn: parent
                width: 32
                height: 2
                radius: 1
                color: "#f4f7fa"
            }

            Label {
                objectName: viewport.objectName.length > 0 ? viewport.objectName + "CloneSourceLabel" : "cloneSourceLabel"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: 4
                text: "Clone source"
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 10
                padding: 4
                background: Rectangle {
                    color: theme.alpha(theme.ink950, 0.82)
                    border.width: theme.chromeBorderWidth
                    border.color: theme.alpha("#7cb5ff", 0.42)
                    radius: 2
                }
            }

            MouseArea {
                id: clonePinMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeAllCursor
                onPressed: function(mouse) {
                    const mapped = mapToItem(viewport, mouse.x, mouse.y)
                    cloneSourcePin.moveSource(
                        (mapped.x - viewport.paintedContentX) / Math.max(1, viewport.paintedContentWidth),
                        (mapped.y - viewport.paintedContentY) / Math.max(1, viewport.paintedContentHeight))
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    const mapped = mapToItem(viewport, mouse.x, mouse.y)
                    cloneSourcePin.moveSource(
                        (mapped.x - viewport.paintedContentX) / Math.max(1, viewport.paintedContentWidth),
                        (mapped.y - viewport.paintedContentY) / Math.max(1, viewport.paintedContentHeight))
                }
            }
        }

        Canvas {
            anchors.fill: previewImage
            visible: viewport.masked
            opacity: 0.23
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "#D4FFF8"
                ctx.lineWidth = 1
                for (let x = -height; x < width; x += 14) {
                    ctx.beginPath()
                    ctx.moveTo(x, height)
                    ctx.lineTo(x + height, 0)
                    ctx.stroke()
                }
            }
        }

        MouseArea {
            id: viewportMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor
            property real pressX: 0
            property real pressY: 0
            property real startPanX: 0
            property real startPanY: 0

            onWheel: function(wheel) {
                if (wheel.angleDelta.y > 0)
                    viewport.setZoom(viewport.zoom * 1.06)
                else if (wheel.angleDelta.y < 0)
                    viewport.setZoom(viewport.zoom / 1.06)
                wheel.accepted = true
            }

            onPressed: function(mouse) {
                pressX = mouse.x
                pressY = mouse.y
                root.loupeX = mouse.x
                root.loupeY = mouse.y
                viewport.updatePixelInspectorAt(mouse.x, mouse.y)
                startPanX = viewport.panX
                startPanY = viewport.panY
                cursorShape = viewport.zoom > 1.0 ? Qt.ClosedHandCursor : Qt.ArrowCursor
            }

            onPositionChanged: function(mouse) {
                viewport.pointerX = mouse.x
                viewport.pointerY = mouse.y
                root.loupeX = mouse.x
                root.loupeY = mouse.y
                viewport.updatePixelInspectorAt(mouse.x, mouse.y)
                if (!pressed || viewport.zoom <= 1.0)
                    return
                viewport.panX = startPanX + mouse.x - pressX
                viewport.panY = startPanY + mouse.y - pressY
            }

            onExited: editorPreview.clearPixelInspector()
            onReleased: cursorShape = viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor
            onCanceled: cursorShape = viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor
            onDoubleClicked: viewport.resetView()
        }
    }

    component SidebarRow: Rectangle {
        id: row
        property string label: ""
        property string value: ""
        property bool active: false
        property string iconName: ""
        signal clicked()
        Layout.fillWidth: true
        Layout.preferredHeight: 34
        radius: 3
        color: active ? theme.alpha(theme.polar, 0.075)
                      : (mouse.containsMouse ? theme.alpha(theme.polar, 0.045) : "transparent")
        border.width: 0
        border.color: "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 2
            visible: row.active
            color: theme.alpha(theme.polar, 0.55)
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10
            AppIcon {
                visible: row.iconName.length > 0
                name: row.iconName
                color: row.active ? theme.textStrong : root.dimText
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
            }
            Label {
                Layout.fillWidth: true
                text: row.label
                color: row.active ? theme.textStrong : (mouse.containsMouse ? theme.textMain : "#A8B1BA")
                font.family: theme.fontUi
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Label {
                visible: row.value.length > 0
                text: row.value
                color: row.active ? theme.textMain : root.dimText
                font.family: theme.fontMono
                font.pixelSize: 10
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: row.clicked()
        }
    }

    component AdjustSlider: InspectorSlider {
        Layout.fillWidth: true
        accent: root.mint
        from: -100
        to: 100
        stepSize: 1
        onResetRequested: valueDragged(defaultValue)
    }

    component RailButton: Button {
        id: rail
        property string iconName: ""
        property bool active: false
        property string tooltip: ""
        signal triggered()
        width: 34
        height: 34
        flat: true
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        ToolTip.visible: hovered && tooltip.length > 0
        ToolTip.delay: 300
        ToolTip.timeout: 4200
        ToolTip.text: tooltip
        onClicked: triggered()

        background: Rectangle {
            color: rail.active ? theme.alpha(theme.polar, 0.08)
                               : (rail.hovered ? theme.alpha(theme.polar, 0.045) : "transparent")
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(theme.polar, rail.active ? 0.54 : 0.0)
            radius: 2
        }

        contentItem: AppIcon {
            name: rail.iconName
            color: rail.active ? theme.textStrong : (rail.hovered ? theme.textMain : root.dimText)
            implicitWidth: 14
            implicitHeight: 14
        }
    }

    component InspectorGroup: ColumnLayout {
        id: group
        default property alias content: groupBody.data
        property string title: ""
        property string resetGroup: ""
        property string resetObjectName: ""
        property bool expanded: true
        Layout.fillWidth: true
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            SectionTitle { Layout.fillWidth: true; label: group.title }
            MiniButton {
                objectName: group.resetObjectName
                visible: group.resetGroup.length > 0
                text: "Reset"
                role: "secondary"
                tooltip: "Reset this panel only."
                onClicked: editorPreview.resetAdjustmentGroup(group.resetGroup)
            }
            Label {
                text: group.expanded ? "v" : ">"
                color: root.dimText
                font.family: theme.fontMono
                font.pixelSize: 10
            }
        }

        ColumnLayout {
            id: groupBody
            Layout.fillWidth: true
            spacing: 6
            visible: group.expanded
        }
    }

    component CollapsibleGroup: ColumnLayout {
        id: group
        default property alias content: body.data
        property string title: ""
        property bool expanded: true
        signal toggled()
        Layout.fillWidth: true
        spacing: 7

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: headerMouse.containsMouse ? "#11191B" : "transparent"

            RowLayout {
                anchors.fill: parent
                spacing: 8

                SectionTitle { Layout.fillWidth: true; label: group.title }
                AppIcon {
                    name: "chevron-right"
                    rotation: group.expanded ? 90 : 0
                    color: root.dimText
                    Layout.preferredWidth: 12
                    Layout.preferredHeight: 12
                }
            }

            MouseArea {
                id: headerMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: group.toggled()
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 4
            visible: group.expanded
        }
    }

    component ScopeTile: Rectangle {
        id: scopeTile
        property string kind: "Waveform"
        property bool large: false
        Layout.fillWidth: true
        Layout.preferredHeight: large ? 280 : 142
        radius: 3
        color: theme.alpha(theme.ink950, 0.52)
        border.width: theme.chromeBorderWidth
        border.color: root.hairline

        Label {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 10
            anchors.topMargin: 8
            text: scopeTile.kind
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
            font.letterSpacing: 0
            z: 2
        }

        HistogramDisplay {
            anchors.fill: parent
            anchors.margins: 10
            anchors.topMargin: 26
            visible: scopeTile.kind === "Histogram"
            redData: editorPreview.histogramRed
            greenData: editorPreview.histogramGreen
            blueData: editorPreview.histogramBlue
            luminanceData: editorPreview.histogramLuminance
            showRgb: root.scopeShowRgb
            showLuminance: root.scopeShowLuma
            logScale: root.scopeLogHistogram
            framed: false
            shadowClipRatio: editorPreview.histogramSampleCount > 0
                             ? editorPreview.histogramShadowClipCount / editorPreview.histogramSampleCount
                             : 0
            highlightClipRatio: editorPreview.histogramSampleCount > 0
                                ? editorPreview.histogramHighlightClipCount / editorPreview.histogramSampleCount
                                : 0
        }

        Canvas {
            id: scopeCanvas
            anchors.fill: parent
            anchors.margins: 10
            anchors.topMargin: 26
            visible: scopeTile.kind !== "Histogram"

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width
                const h = height
                const red = editorPreview.scopeWaveformRed || []
                const green = editorPreview.scopeWaveformGreen || []
                const blue = editorPreview.scopeWaveformBlue || []
                const luma = editorPreview.scopeWaveformLuminance || []
                const densityRed = editorPreview.scopeDensityRed || []
                const densityGreen = editorPreview.scopeDensityGreen || []
                const densityBlue = editorPreview.scopeDensityBlue || []
                const densityLuma = editorPreview.scopeDensityLuminance || []
                const vectors = editorPreview.scopeVectorscopePoints || []
                const densityLevels = 64

                function drawGrid(x, y, plotWidth, plotHeight, labels) {
                    if (!root.scopeShowGrid)
                        return
                    ctx.save()
                    ctx.lineWidth = 1
                    ctx.strokeStyle = theme.alpha(theme.polar, 0.075)
                    for (let i = 1; i < 4; ++i) {
                        const gx = Math.round(x + (plotWidth / 4) * i) + 0.5
                        ctx.beginPath()
                        ctx.moveTo(gx, y)
                        ctx.lineTo(gx, y + plotHeight)
                        ctx.stroke()
                    }
                    for (let j = 0; j <= 4; ++j) {
                        const gy = Math.round(y + (plotHeight / 4) * j) + 0.5
                        ctx.beginPath()
                        ctx.moveTo(x, gy)
                        ctx.lineTo(x + plotWidth, gy)
                        ctx.stroke()
                    }
                    ctx.strokeStyle = theme.alpha(theme.lineSoft, 0.20)
                    ctx.strokeRect(Math.round(x) + 0.5, Math.round(y) + 0.5, Math.max(0, plotWidth - 1), Math.max(0, plotHeight - 1))
                    if (labels) {
                        ctx.fillStyle = theme.alpha(theme.textFaint, 0.52)
                        ctx.font = "8px \"" + theme.fontMono + "\""
                        ctx.textAlign = "left"
                        ctx.textBaseline = "middle"
                        const values = [["100", 0], ["75", 0.25], ["50", 0.5], ["25", 0.75], ["0", 1]]
                        for (let k = 0; k < values.length; ++k)
                            ctx.fillText(values[k][0], 0, y + values[k][1] * plotHeight)
                    }
                    ctx.restore()
                }

                function channelValue(data, index, key) {
                    if (!data || data.length === 0)
                        return 0
                    const raw = data[Math.max(0, Math.min(data.length - 1, index))] || 0
                    const value = typeof raw === "object" ? Number(raw[key] || 0) : Number(raw || 0)
                    return Math.max(0, Math.min(1, value))
                }

                function drawAverage(data, color, x, y, plotWidth, plotHeight, alpha, lineWidth) {
                    if (!root.scopeShowTrace)
                        return
                    const count = data ? data.length : 0
                    if (count < 2)
                        return
                    const step = plotWidth / Math.max(1, count - 1)
                    ctx.save()
                    ctx.strokeStyle = theme.alpha(color, alpha)
                    ctx.lineWidth = lineWidth
                    ctx.beginPath()
                    for (let i = 0; i < count; ++i) {
                        const px = x + step * i
                        const py = y + plotHeight - channelValue(data, i, "avg") * plotHeight
                        if (i === 0)
                            ctx.moveTo(px, py)
                        else
                            ctx.lineTo(px, py)
                    }
                    ctx.stroke()
                    ctx.restore()
                }

                function drawDensity(data, color, x, y, plotWidth, plotHeight, alphaScale, pointScale) {
                    if (!root.scopeShowDensity || !data || data.length < densityLevels)
                        return
                    const columns = Math.max(1, Math.floor(data.length / densityLevels))
                    const xStep = plotWidth / columns
                    const yStep = plotHeight / densityLevels
                    ctx.save()
                    ctx.globalCompositeOperation = "lighter"
                    for (let column = 0; column < columns; ++column) {
                        const px = x + column * xStep
                        for (let level = 0; level < densityLevels; ++level) {
                            const density = Number(data[column * densityLevels + level] || 0)
                            if (density <= 0.008)
                                continue
                            const alpha = Math.min(0.72, Math.pow(density, 1.35) * alphaScale)
                            if (alpha <= 0.006)
                                continue
                            ctx.fillStyle = theme.alpha(color, alpha)
                            ctx.fillRect(px, y + plotHeight - (level + 1) * yStep, Math.max(1, xStep * pointScale), Math.max(1, yStep * 1.05))
                        }
                    }
                    ctx.restore()
                }

                if (scopeTile.kind === "Parade") {
                    const labelWidth = 20
                    const gap = 8
                    const plotX = labelWidth
                    const segment = (w - labelWidth - gap * 2) / 3
                    drawGrid(plotX, 0, w - plotX, h, true)
                    if (root.scopeShowRgb) {
                        drawDensity(densityRed, theme.signalRed, plotX, 0, segment, h, 0.70, 1.08)
                        drawDensity(densityGreen, theme.vectorGreen, plotX + segment + gap, 0, segment, h, 0.62, 1.08)
                        drawDensity(densityBlue, theme.signalCyan, plotX + segment * 2 + gap * 2, 0, segment, h, 0.62, 1.08)
                        drawAverage(red, theme.signalRed, plotX, 0, segment, h, 0.62, 1.1)
                        drawAverage(green, theme.vectorGreen, plotX + segment + gap, 0, segment, h, 0.58, 1.1)
                        drawAverage(blue, theme.signalCyan, plotX + segment * 2 + gap * 2, 0, segment, h, 0.58, 1.1)
                    }
                    return
                }

                if (scopeTile.kind === "Vectorscope") {
                    const cx = w / 2
                    const cy = h / 2
                    const radius = Math.min(w, h) * 0.40
                    ctx.save()
                    ctx.strokeStyle = theme.alpha(theme.polar, 0.18)
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.strokeStyle = theme.alpha(theme.polar, 0.075)
                    ctx.beginPath()
                    ctx.arc(cx, cy, radius * 0.55, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.moveTo(cx - radius, cy)
                    ctx.lineTo(cx + radius, cy)
                    ctx.moveTo(cx, cy - radius)
                    ctx.lineTo(cx, cy + radius)
                    ctx.stroke()
                    ctx.restore()
                    ctx.save()
                    ctx.globalCompositeOperation = "lighter"
                    for (let i = 0; i < vectors.length; ++i) {
                        const point = vectors[i]
                        const px = cx + (Number(point.x || 0.5) - 0.5) * radius * 1.75
                        const py = cy + (Number(point.y || 0.5) - 0.5) * radius * 1.75
                        ctx.fillStyle = Qt.rgba(Number(point.red || 0.8), Number(point.green || 0.8), Number(point.blue || 0.8), 0.045 + Number(point.luma || 0.5) * 0.07)
                        ctx.fillRect(px - 0.65, py - 0.65, 1.5, 1.5)
                    }
                    ctx.fillStyle = theme.alpha(theme.polar, 0.36)
                    ctx.fillRect(cx - 1.1, cy - 1.1, 2.2, 2.2)
                    ctx.restore()
                    return
                }

                const plotX = 20
                drawGrid(plotX, 0, w - plotX, h, true)
                if (root.scopeShowLuma) {
                    drawDensity(densityLuma, theme.polar, plotX, 0, w - plotX, h, 0.46, 1.06)
                    drawAverage(luma, theme.polar, plotX, 0, w - plotX, h, 0.58, 1.1)
                }
                if (root.scopeShowRgb) {
                    drawDensity(densityBlue, theme.signalCyan, plotX, 0, w - plotX, h, 0.16, 1.02)
                    drawDensity(densityRed, theme.signalAmber, plotX, 0, w - plotX, h, 0.13, 1.02)
                    drawAverage(blue, theme.signalCyan, plotX, 0, w - plotX, h, 0.24, 0.9)
                    drawAverage(red, theme.signalAmber, plotX, 0, w - plotX, h, 0.20, 0.9)
                }
            }

            Component.onCompleted: requestPaint()

            Connections {
                target: editorPreview
                function onHistogramChanged() { scopeCanvas.requestPaint() }
            }
            Connections {
                target: root
                function onScopeShowRgbChanged() { scopeCanvas.requestPaint() }
                function onScopeShowLumaChanged() { scopeCanvas.requestPaint() }
                function onScopeShowDensityChanged() { scopeCanvas.requestPaint() }
                function onScopeShowTraceChanged() { scopeCanvas.requestPaint() }
                function onScopeShowGridChanged() { scopeCanvas.requestPaint() }
            }
        }
    }

    component ScopeGrid: GridLayout {
        id: grid
        objectName: large ? "largeScopeGrid" : "inspectorScopeGrid"
        property bool large: false
        readonly property int effectiveColumns: columns
        Layout.fillWidth: true
        columns: large && root.scopeLayoutPreset === "Quad" ? 2 : 1
        columnSpacing: 10
        rowSpacing: 10
        Repeater {
            model: root.scopeAnalysisModes()
            ScopeTile {
                required property string modelData
                kind: modelData
                large: grid.large
            }
        }
    }

    component EmptyStateCard: Rectangle {
        id: emptyState
        property string iconName: "library"
        property string title: ""
        property string message: ""
        property bool error: false
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 220
        color: theme.alpha(theme.ink950, 0.62)
        border.width: theme.chromeBorderWidth
        border.color: error ? theme.alpha(theme.error, 0.72) : root.hairline
        radius: 4

        Column {
            anchors.centerIn: parent
            width: Math.min(420, Math.max(220, parent.width - 48))
            spacing: 12

            AppIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: emptyState.iconName
                width: 30
                height: 30
                color: emptyState.error ? theme.error : root.mint
            }

            Label {
                width: parent.width
                text: emptyState.title
                color: theme.textStrong
                horizontalAlignment: Text.AlignHCenter
                font.family: theme.fontUi
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Label {
                width: parent.width
                text: emptyState.message
                color: emptyState.error ? theme.error : root.dimText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: 11
            }
        }
    }

    component HealthStatusStrip: Rectangle {
        id: healthStrip
        objectName: "healthStatusStrip"
        width: healthRow.implicitWidth + 18
        height: 34
        radius: 4
        color: theme.alpha(theme.ink950, 0.82)
        border.width: theme.chromeBorderWidth
        border.color: root.hairlineStrong

        HoverHandler { id: healthHover }

        RowLayout {
            id: healthRow
            anchors.centerIn: parent
            spacing: 8

            Repeater {
                model: root.systemHealthItems()

                delegate: RowLayout {
                    required property var modelData
                    spacing: 4

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: root.healthStateColor(modelData.state)
                    }

                    Label {
                        text: modelData.label
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }

                    ToolTip.visible: healthHover.hovered
                    ToolTip.delay: 350
                    ToolTip.timeout: 4200
                    ToolTip.text: modelData.label + " " + modelData.state + ": " + modelData.detail
                }
            }
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.page === "library" ? libraryPage
                         : root.page === "edit" ? editPage
                         : exportPage
    }

    HealthStatusStrip {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 12
        anchors.rightMargin: 12
        z: 120
    }

    Component {
        id: libraryPage

        RowLayout {
            spacing: 0

            Panel {
                objectName: "librarySourcesPanel"
                visible: root.librarySourcesPanelVisible
                Layout.preferredWidth: root.librarySourcesPanelVisible ? root.librarySourcesPanelWidth : 0
                Layout.fillHeight: true
                title: "Sources"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    SidebarRow {
                        label: "VIREL Vault"
                        value: root.formatInt(catalogAssets.totalAssetCount)
                        iconName: "cloud"
                        active: !root.importModeOpen
                        onClicked: root.importModeOpen = false
                    }
                    SidebarRow {
                        label: "Local Sources"
                        value: root.liveSourceSummary()
                        iconName: "drive"
                        active: root.importModeOpen
                        onClicked: root.startVaultImport()
                    }
                    MiniButton {
                        Layout.fillWidth: true
                        role: "primary"
                        text: catalogAssets.importing ? "Importing..." : "Import to VIREL Vault"
                        iconName: "plus"
                        active: root.importModeOpen
                        enabled: !catalogAssets.busy || catalogAssets.importing
                        onClicked: root.startVaultImport()
                    }
                    MiniButton {
                        Layout.fillWidth: true
                        text: "Import Folder..."
                        iconName: "folder"
                        role: "secondary"
                        enabled: !catalogAssets.busy
                        onClicked: root.importFolderToVault()
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.importStatusText()
                        color: catalogAssets.errorMessage.length > 0 ? theme.error : root.dimText
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    RowRule {}
                    SectionTitle { label: "Import Sources"; Layout.topMargin: 6 }
                    Repeater {
                        model: catalogAssets.importSources
                        SidebarRow {
                            required property var modelData
                            label: String(modelData.label || modelData.path || "Source")
                            value: String(modelData.subtitle || modelData.kind || "")
                            iconName: String(modelData.kind || "") === "volume" ? "drive" : "folder"
                            active: false
                            onClicked: root.startVaultImport()
                        }
                    }
                    RowRule {}
                    SectionTitle { label: "Smart Collections"; Layout.topMargin: 10 }
                    SidebarRow { label: "All Images"; value: root.formatInt(catalogAssets.totalAssetCount); onClicked: catalogAssets.clearFilters() }
                    SidebarRow { label: "Visible"; value: root.formatInt(catalogAssets.assetCount) }
                    SidebarRow { label: "Picked"; value: root.formatInt(catalogAssets.visiblePickedCount); onClicked: catalogAssets.reviewFilter = "picked" }
                    SidebarRow { label: "Rejected"; value: root.formatInt(catalogAssets.visibleRejectedCount); onClicked: catalogAssets.reviewFilter = "rejected" }
                    SidebarRow { label: "Rated"; value: root.formatInt(catalogAssets.visibleRatedCount); onClicked: catalogAssets.reviewFilter = "rated" }
                    SidebarRow { label: "Unrated"; value: root.formatInt(catalogAssets.visibleUnratedCount) }
                    SidebarRow { label: "Unmarked"; value: root.formatInt(catalogAssets.visibleUnmarkedCount); onClicked: catalogAssets.reviewFilter = "unmarked" }
                    SidebarRow { label: "Online"; value: root.formatInt(catalogAssets.visibleOnlineCount); onClicked: catalogAssets.availabilityFilter = "online" }
                    SidebarRow { label: "Offline"; value: root.formatInt(catalogAssets.visibleOfflineCount); onClicked: catalogAssets.availabilityFilter = "offline" }
                    RowRule {}
                    SectionTitle { label: "Filters"; Layout.topMargin: 10 }
                    Repeater {
                        model: ["Media Type", "Camera", "Lens", "Date Captured", "Rating", "Pick Status", "Color Label", "Keywords"]
                        SidebarRow { required property string modelData; label: modelData; value: "v" }
                    }
                    Item { Layout.fillHeight: true }
                    Label { text: "Catalog"; color: root.dimText; font.family: theme.fontUi; font.pixelSize: 10; font.letterSpacing: 0 }
                    ProgressBar {
                        Layout.fillWidth: true
                        value: catalogAssets.totalAssetCount > 0
                               ? Math.max(0.02, Math.min(1, catalogAssets.assetCount / catalogAssets.totalAssetCount))
                               : 0
                    }
                    Label {
                        text: root.formatInt(catalogAssets.assetCount) + " visible / "
                              + root.formatInt(catalogAssets.totalAssetCount) + " total  -  "
                              + root.formatInt(catalogAssets.driveCount) + " drives"
                        color: root.dimText
                        font.pixelSize: 10
                        font.family: theme.fontUi
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 0; Layout.fillHeight: true; color: "transparent" }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14
                Layout.margins: 18

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TinyInput {
                        Layout.fillWidth: true
                        placeholder: "Search images..."
                        text: root.searchText
                        onTextChanged: root.searchText = text
                    }
                    MiniButton {
                        visible: root.importModeOpen
                        role: "ghost"
                        text: "Back to Library"
                        iconName: "chevron-left"
                        onClicked: root.importModeOpen = false
                    }
                    MiniButton { text: "All Media"; iconName: "filter"; role: "segmented"; onClicked: catalogAssets.mediaTypeFilter = "all" }
                    MiniButton { text: "All Projects"; iconName: "folder"; role: "segmented"; onClicked: catalogAssets.projectFilter = "all" }
                    MiniButton { text: "All Time"; iconName: "clock"; role: "segmented" }
                    MiniButton { text: "More Filters"; iconName: "adjust"; role: "segmented" }
                    MiniButton { text: "Sources"; role: "segmented"; active: root.librarySourcesPanelVisible; onClicked: root.librarySourcesPanelVisible = !root.librarySourcesPanelVisible }
                    MiniButton { text: "Inspector"; role: "segmented"; active: root.libraryInspectorPanelVisible; onClicked: root.libraryInspectorPanelVisible = !root.libraryInspectorPanelVisible }
                    MiniButton { text: root.compactMode ? "Pro" : "Compact"; role: "segmented"; active: root.compactMode; onClicked: root.toggleInterfaceMode() }
                }

                RowLayout {
                    visible: !root.importModeOpen
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: root.formatInt(root.assetCount()) + " results"
                        color: root.dimText
                        font.family: theme.fontUi
                        font.pixelSize: 12
                    }
                    MiniButton { text: "Grid"; iconName: "grid"; role: "segmented"; active: root.catalogViewMode === "grid"; onClicked: root.catalogViewMode = "grid" }
                    MiniButton { text: "Survey"; iconName: "survey"; role: "segmented"; active: root.catalogViewMode === "survey"; onClicked: root.catalogViewMode = "survey" }
                    MiniButton { text: "List"; iconName: "list"; role: "segmented" }
                    MiniButton {
                        role: "segmented"
                        text: root.selectionMode ? "Done" : "Select"
                        active: root.selectionMode
                        onClicked: root.selectionMode = !root.selectionMode
                    }
                    MiniButton { text: "..."; square: true; role: "icon" }
                }

                GridView {
                    id: libraryGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !root.importModeOpen && root.catalogViewMode === "grid" && root.assetCount() > 0
                    clip: true
                    cellWidth: Math.max(224, Math.floor(width / Math.max(1, Math.floor(width / 250))))
                    cellHeight: 206
                    model: catalogAssets

                    function updateVisibleAssetRange() {
                        if (!catalogAssets)
                            return
                        if (!visible || count <= 0 || cellWidth <= 0 || cellHeight <= 0) {
                            catalogAssets.setVisibleAssetRange(-1, -1)
                            return
                        }
                        const columns = Math.max(1, Math.floor(width / cellWidth))
                        const firstRow = Math.max(0, Math.floor(contentY / cellHeight))
                        const visibleRows = Math.max(1, Math.ceil(height / cellHeight) + 1)
                        const first = Math.max(0, firstRow * columns)
                        const last = Math.min(count - 1, ((firstRow + visibleRows) * columns) - 1)
                        catalogAssets.setVisibleAssetRange(first, last)
                    }

                    Component.onCompleted: updateVisibleAssetRange()
                    onContentYChanged: updateVisibleAssetRange()
                    onHeightChanged: updateVisibleAssetRange()
                    onWidthChanged: updateVisibleAssetRange()
                    onCountChanged: updateVisibleAssetRange()
                    onVisibleChanged: updateVisibleAssetRange()

                    delegate: ThumbnailCard {
                        required property int index
                        required property string assetId
                        width: libraryGrid.cellWidth - 16
                        height: 188
                        assetIndex: index
                        selected: catalogAssets.assetSelected(assetId)
                        onActivated: root.selectAsset(index)
                        onDoubleActivated: {
                            root.selectAsset(index)
                            root.assetActivated("edit")
                        }
                    }
                }

                GridLayout {
                    id: librarySurvey
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !root.importModeOpen && root.catalogViewMode === "survey" && root.assetCount() > 0
                    columns: width > 1200 ? 3 : 2
                    columnSpacing: 10
                    rowSpacing: 10

                    Repeater {
                        id: surveyAssetRepeater
                        model: root.catalogAssets.surveyAssetCount(root.catalogAssets.selectedIndex, 6)
                        Component.onCompleted: root.surveyAssetCount = count
                        onCountChanged: root.surveyAssetCount = count

                        delegate: ReviewPreview {
                            required property int index
                            readonly property var surveyAsset: root.catalogAssets.surveyAssetAt(root.catalogAssets.selectedIndex, index, 6)

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 240
                            title: String(surveyAsset.cameraLabel || root.assetName(surveyAsset.visibleIndex))
                            subtitle: String(surveyAsset.captureDate || "")
                            imageSource: String(surveyAsset.thumbnailSource || "")
                            selected: root.catalogAssets.assetSelected(surveyAsset.assetId)
                            photoAsset: String(surveyAsset.mediaType || "photo") === "photo"
                            rating: Number(surveyAsset.rating || 0)
                            picked: Boolean(surveyAsset.picked)
                            rejected: Boolean(surveyAsset.rejected)
                            burstText: surveyAsset.burstSize && surveyAsset.burstSize > 1
                                       ? ("B " + surveyAsset.burstPosition + "/" + surveyAsset.burstSize)
                                       : ""
                            onActivated: root.selectAsset(surveyAsset.visibleIndex)
                            onDoubleActivated: {
                                root.selectAsset(surveyAsset.visibleIndex)
                                root.assetActivated(surveyAsset.mediaType === "photo" ? "edit" : "timeline")
                            }
                        }
                    }
                }

                EmptyStateCard {
                    objectName: "libraryEmptyState"
                    visible: !root.importModeOpen && root.assetCount() === 0
                    iconName: catalogAssets.errorMessage.length > 0 ? "delete" : "library"
                    title: root.libraryEmptyStateTitle()
                    message: root.libraryEmptyStateMessage()
                    error: catalogAssets.errorMessage.length > 0
                }

                ImportView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.importModeOpen
                }
            }

            Rectangle { Layout.preferredWidth: 0; Layout.fillHeight: true; color: "transparent" }

            Panel {
                objectName: "libraryInspectorPanel"
                visible: root.libraryInspectorPanelVisible
                Layout.preferredWidth: root.libraryInspectorPanelVisible ? root.libraryInspectorPanelWidth : 0
                Layout.fillHeight: true
                title: "Inspector"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    ImageViewport {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 164
                        source: root.displayImageSource(0)
                    }
                    Label { Layout.fillWidth: true; text: root.selectedName(); color: theme.textStrong; font.pixelSize: 14; font.family: theme.fontUi; elide: Text.ElideRight }
                    Row {
                        spacing: 8
                        Repeater { model: 5; Label { text: "*"; color: root.dimText; font.pixelSize: 16 } }
                        Rectangle { width: 11; height: 11; radius: 2; color: root.mint; anchors.verticalCenter: parent.verticalCenter }
                    }
                    RowRule {}
                    SectionTitle { label: "Metadata" }
                    Repeater {
                        model: [
                            ["Camera", "DJI \"ZH20T\""],
                            ["Lens", "6.8-119.9 mm f/4.0"],
                            ["Captured", "Oct 1, 2025 9:57 PM"],
                            ["Dimensions", "5472 x 3648"],
                            ["File Size", "32.4 MB"],
                            ["Color Profile", "sRGB IEC61966-2.1"],
                            ["Imported", "Apr 8, 2026 9:57 PM"]
                        ]
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { Layout.preferredWidth: 110; text: modelData[0]; color: root.dimText; font.pixelSize: 11 }
                            Label { Layout.fillWidth: true; text: modelData[1]; color: theme.textMain; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                    }
                    RowRule {}
                    SectionTitle { label: "Actions" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8
                        MiniButton { Layout.fillWidth: true; text: "Edit"; iconName: "brush-tool"; role: "secondary"; onClicked: root.assetActivated("edit") }
                        MiniButton { Layout.fillWidth: true; text: "Masks"; iconName: "masks"; role: "secondary"; onClicked: root.assetActivated("masks") }
                        MiniButton { Layout.fillWidth: true; text: "Compare"; iconName: "compare"; role: "secondary"; onClicked: { root.assetActivated("edit"); root.activateEditTool("compare") } }
                        MiniButton { Layout.fillWidth: true; text: "Export"; iconName: "export"; role: "primary"; onClicked: root.assetActivated("export") }
                    }
                    SectionTitle { label: "Pick Status" }
                    RowLayout {
                        Layout.fillWidth: true
                        MiniButton { Layout.fillWidth: true; text: "Pick"; role: "segmented"; active: catalogAssets.selectedPicked; onClicked: catalogAssets.toggleSelectedPick() }
                        MiniButton { Layout.fillWidth: true; text: "Reject"; role: "segmented"; active: catalogAssets.selectedRejected; onClicked: catalogAssets.toggleSelectedReject() }
                        MiniButton { Layout.fillWidth: true; text: "Unmarked"; role: "segmented"; active: !catalogAssets.selectedPicked && !catalogAssets.selectedRejected; onClicked: catalogAssets.clearSelectedCullState() }
                    }
                    SectionTitle { label: "Rating" }
                    RowLayout {
                        Layout.fillWidth: true
                        Repeater {
                            model: 5
                            MiniButton { required property int index; Layout.fillWidth: true; text: String(index + 1); role: "segmented"; active: catalogAssets.selectedRating >= index + 1; onClicked: catalogAssets.setSelectedRating(index + 1) }
                        }
                    }
                    TinyInput { Layout.fillWidth: true; placeholder: "Add keywords..." }
                }
            }
        }
    }

    Component {
        id: editPage

        RowLayout {
            spacing: 8

            Rectangle {
                visible: root.editingChromeVisible
                Layout.preferredWidth: 44
                Layout.fillHeight: true
                color: "#07090A"
                border.width: theme.chromeBorderWidth
                border.color: root.hairline

                Column {
                    anchors.centerIn: parent
                    spacing: 16
                    Repeater {
                        model: [
                            { icon: "adjust", tool: "adjust", label: "Adjust" },
                            { icon: "crop", tool: "crop", label: "Crop" },
                            { icon: "masks", tool: "masks", label: "Masks" },
                            { icon: "brush-tool", tool: "brush", label: "Brush" },
                            { icon: "compare", tool: "compare", label: "Compare" },
                            { icon: "export", tool: "export", label: "Export" }
                        ]
                        RailButton {
                            required property var modelData
                            iconName: modelData.icon
                            tooltip: modelData.label
                            active: root.editTool === modelData.tool
                            onTriggered: root.activateEditTool(modelData.tool)
                        }
                    }
                }

                MiniButton {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: 12
                    square: true
                    iconName: root.editSidebarCollapsed ? "chevron-right" : "chevron-left"
                    role: "icon"
                    onClicked: root.editSidebarCollapsed = !root.editSidebarCollapsed
                }
            }

            Panel {
                objectName: "editContextPanel"
                visible: root.editContextPanelActive
                Layout.preferredWidth: visible ? root.editContextPanelWidth : 0
                Layout.fillHeight: true
                title: "Asset Context"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    Label { Layout.fillWidth: true; text: root.selectedName(); color: theme.textMain; font.pixelSize: 12; elide: Text.ElideMiddle }
                    RowRule {}
                    Repeater {
                        model: [
                            ["Captured", "Oct 1, 2025 9:57 PM"],
                            ["Camera", editorPreview.cameraLabel.length > 0 ? editorPreview.cameraLabel : "DJI \"ZH20T\""],
                            ["Lens", editorPreview.lensModel.length > 0 ? editorPreview.lensModel : "6.8-119.9 mm f/4.0"],
                            ["Resolution", "1676 x 1257"],
                            ["Color Space", "sRGB  IEC61966-2.1"]
                        ]
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { Layout.preferredWidth: 82; text: modelData[0]; color: root.dimText; font.pixelSize: 11 }
                            Label { Layout.fillWidth: true; text: modelData[1]; color: theme.textMain; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                    }
                    RowRule {}
                    CollapsibleGroup {
                        title: "Library"
                        expanded: root.editProjectsExpanded
                        onToggled: root.editProjectsExpanded = !root.editProjectsExpanded

                        SectionTitle { label: "Projects" }
                        SidebarRow {
                            label: "All Projects"
                            value: root.formatInt(catalogAssets.assetCount)
                            iconName: "library"
                            active: catalogAssets.projectFilter === "all"
                            onClicked: catalogAssets.projectFilter = "all"
                        }
                        SidebarRow {
                            label: "Unassigned"
                            value: root.formatInt(catalogAssets.assetCountForProject("unassigned"))
                            iconName: "library"
                            active: catalogAssets.projectFilter === "unassigned"
                            onClicked: catalogAssets.projectFilter = "unassigned"
                        }
                        Repeater {
                            model: catalogAssets.projectNames
                            SidebarRow {
                                required property string modelData
                                label: modelData
                                value: root.formatInt(catalogAssets.assetCountForProject(modelData))
                                iconName: "library"
                                active: catalogAssets.projectFilter === modelData
                                onClicked: catalogAssets.projectFilter = modelData
                            }
                        }

                        RowRule { Layout.topMargin: 6; Layout.bottomMargin: 4 }
                        SectionTitle { label: "Collections" }
                        SidebarRow { label: "All Photos"; value: root.formatInt(catalogAssets.totalAssetCount); iconName: "grid"; active: catalogAssets.reviewFilter === "all" && catalogAssets.minimumRatingFilter === 0; onClicked: catalogAssets.clearFilters() }
                        SidebarRow { label: "Picked"; value: root.formatInt(catalogAssets.visiblePickedCount); iconName: "select"; active: catalogAssets.reviewFilter === "picked"; onClicked: catalogAssets.reviewFilter = "picked" }
                        SidebarRow { label: "Rejected"; value: root.formatInt(catalogAssets.visibleRejectedCount); iconName: "delete"; active: catalogAssets.reviewFilter === "rejected"; onClicked: catalogAssets.reviewFilter = "rejected" }
                        SidebarRow { label: "Rated"; value: root.formatInt(catalogAssets.visibleRatedCount); iconName: "survey"; active: catalogAssets.reviewFilter === "rated"; onClicked: catalogAssets.reviewFilter = "rated" }
                        SidebarRow { label: "Five Star"; value: ""; iconName: "survey"; active: catalogAssets.minimumRatingFilter === 5; onClicked: catalogAssets.minimumRatingFilter = 5 }
                    }
                    Item { Layout.fillHeight: true }
                    RowRule {}
                    Label { text: "Storage"; color: root.dimText; font.family: theme.fontUi; font.pixelSize: 10; font.letterSpacing: 0 }
                    ProgressBar { Layout.fillWidth: true; value: catalogAssets.totalAssetCount > 0 ? Math.min(1, catalogAssets.assetCount / catalogAssets.totalAssetCount) : 0 }
                    Label {
                        Layout.fillWidth: true
                        text: root.formatInt(catalogAssets.assetCount) + " visible / " + root.formatInt(catalogAssets.totalAssetCount) + " total"
                        color: root.dimText
                        font.family: theme.fontUi
                        font.pixelSize: 10
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Rectangle {
                    objectName: "maskCreationToolbar"
                    visible: root.editTool === "masks" || root.editTool === "brush"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 72 : 0
                    color: root.panelColor
                    border.width: theme.chromeBorderWidth
                    border.color: root.hairline
                    radius: 4

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: "Source"
                                color: root.dimText
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0
                            }
                            MiniButton { objectName: "maskSourceObjectButton"; text: "Object"; iconName: "smart-select"; role: "segmented"; active: root.editTool === "masks"; tooltip: "Create an object selection source."; onClicked: root.createMaskSource("object") }
                            MiniButton { objectName: "maskSourceBrushButton"; text: "Brush"; iconName: "brush-tool"; role: "segmented"; active: root.editTool === "brush"; tooltip: "Paint a manual mask source."; onClicked: root.startMaskBrushSource() }
                            MiniButton { objectName: "maskSourceLassoButton"; text: "Lasso"; iconName: "lasso"; role: "segmented"; tooltip: "Draw a freeform selection source."; onClicked: { root.editTool = "masks"; editorPreview.maskPreviewMode = "overlay"; editorPreview.startLassoSelection(root.maskCreationOperation === "subtract") } }
                            MiniButton { objectName: "maskSourceLinearButton"; text: "Linear"; iconName: "geometry"; role: "segmented"; tooltip: "Create a linear gradient mask source."; onClicked: root.createMaskSource("linear") }
                            MiniButton { objectName: "maskSourceRadialButton"; text: "Radial"; iconName: "masks"; role: "segmented"; tooltip: "Create a radial mask source."; onClicked: root.createMaskSource("radial") }
                            Item { Layout.fillWidth: true }
                            MiniButton { text: "Done"; role: "primary"; active: editorPreview.hasActiveMask; enabled: editorPreview.hasActiveMask; onClicked: editorPreview.deselectActiveMask() }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: "Mode"
                                color: root.dimText
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0
                            }
                            MiniButton { objectName: "maskCombineAddButton"; text: "Add"; iconName: "plus"; role: "segmented"; active: root.maskCreationOperation === "add"; tooltip: "Add the next source to the active mask set."; onClicked: root.setMaskCreationOperation("add") }
                            MiniButton { objectName: "maskCombineSubtractButton"; text: "Subtract"; iconName: "minus"; role: "segmented"; active: root.maskCreationOperation === "subtract"; tooltip: "Subtract the next source from the active mask set."; onClicked: root.setMaskCreationOperation("subtract") }
                            MiniButton { objectName: "maskCombineIntersectButton"; text: "Intersect"; iconName: "compare"; role: "segmented"; active: root.maskCreationOperation === "intersect"; tooltip: "Keep only the overlap with the next source."; onClicked: root.setMaskCreationOperation("intersect") }
                            Label {
                                objectName: "activeMaskToolbarStatusLabel"
                                Layout.fillWidth: true
                                text: root.activeMaskToolbarStatusText()
                                horizontalAlignment: Text.AlignRight
                                color: editorPreview.hasActiveMask ? root.mint : root.dimText
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "maskStackStrip"
                    visible: (root.editTool === "masks" || root.editTool === "brush") && editorPreview.maskSetCount > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 56 : 0
                    color: root.panelColor
                    border.width: theme.chromeBorderWidth
                    border.color: root.hairline
                    radius: 4
                    clip: true

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        Label {
                            text: "Stack"
                            color: root.dimText
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0
                        }

                        Repeater {
                            model: editorPreview.maskSets

                            delegate: Rectangle {
                                required property var modelData

                                readonly property string thumbnailSource: String(modelData.thumbnailSource || "")
                                readonly property bool isActiveSet: {
                                    for (let sourceIndex = 0; sourceIndex < modelData.sources.length; ++sourceIndex) {
                                        if (modelData.sources[sourceIndex].flatIndex === editorPreview.activeMaskIndex)
                                            return true
                                    }
                                    return false
                                }

                                objectName: "maskStackSetChip"
                                Layout.preferredWidth: 176
                                Layout.preferredHeight: 44
                                radius: 4
                                color: isActiveSet ? theme.alpha(root.mint, 0.10) : "#11171A"
                                border.width: 1
                                border.color: isActiveSet ? root.mint : root.hairline
                                opacity: modelData.enabled ? 1.0 : 0.56

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 7

                                    Rectangle {
                                        objectName: "maskStackSetThumbnail"
                                        Layout.preferredWidth: 38
                                        Layout.preferredHeight: 28
                                        radius: 3
                                        color: "#070A0C"
                                        border.width: 1
                                        border.color: theme.alpha(root.mint, 0.42)
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            source: thumbnailSource
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: false
                                            visible: source.toString().length > 0
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Label {
                                            objectName: "maskStackSetLabel"
                                            Layout.fillWidth: true
                                            text: String(modelData.name || "Mask")
                                            color: theme.textMain
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "maskStackSetSummaryLabel"
                                            Layout.fillWidth: true
                                            text: root.maskStackSourceSummary(modelData)
                                            color: root.dimText
                                            font.family: theme.fontUi
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                ImageViewport {
                    id: editViewport
                    objectName: "editImageViewport"
                    readonly property int toolbarCompactWidth: 1240
                    readonly property int toolbarTightWidth: 1000
                    readonly property int toolbarStackWidth: 1320
                    readonly property bool toolbarCompact: width < toolbarCompactWidth
                    readonly property bool toolbarTight: width < toolbarTightWidth
                    readonly property bool toolbarStacked: width < toolbarStackWidth
                    visible: root.editTool !== "compare" && !(root.largeScopesMode && root.editInspectorTab === "scopes")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    source: root.displayImageSource(0)
                    resetKey: catalogAssets.selectedAssetId
                    masked: root.editTool === "masks" || root.editTool === "brush"
                    loupeEnabled: true
                    focusCheckOverlayEnabled: true
                    focusPeakingOverlayEnabled: true
                    clippingOverlayEnabled: true
                    falseColorOverlayEnabled: true
                    gamutOverlayEnabled: true
                    zoneMapOverlayEnabled: true

                    CropOverlay {
                        objectName: "cropBoxOverlay"
                        anchors.fill: parent
                        active: root.editTool === "crop" && root.cropToolMode === "crop" && editorPreview.cropEditing && !editorPreview.compareMode
                        imageX: editViewport.paintedContentX
                        imageY: editViewport.paintedContentY
                        imageWidth: editViewport.paintedContentWidth
                        imageHeight: editViewport.paintedContentHeight
                        cropX: editorPreview.cropRectX
                        cropY: editorPreview.cropRectY
                        cropWidth: editorPreview.cropRectWidth
                        cropHeight: editorPreview.cropRectHeight
                        aspectRatio: editorPreview.cropAspectRatio
                        guideMode: root.cropGuideMode
                        onRectEdited: function(x, y, width, height) {
                            editorPreview.updateCropRect(x, y, width, height)
                        }
                    }

                    HorizonStraightenOverlay {
                        objectName: "cropHorizonOverlay"
                        anchors.fill: parent
                        active: root.editTool === "crop" && root.cropToolMode === "horizon" && !editorPreview.cropEditing && !editorPreview.compareMode
                        imageX: editViewport.paintedContentX
                        imageY: editViewport.paintedContentY
                        imageWidth: editViewport.paintedContentWidth
                        imageHeight: editViewport.paintedContentHeight
                        onHorizonEdited: function(startX, startY, endX, endY) {
                            root.applyHorizonStraightenGuide(startX, startY, endX, endY)
                        }
                    }

                    TransformOverlay {
                        objectName: "cropTransformOverlay"
                        anchors.fill: parent
                        active: root.editTool === "crop" && root.cropToolMode === "transform" && !editorPreview.cropEditing && !editorPreview.compareMode
                        imageX: editViewport.paintedContentX
                        imageY: editViewport.paintedContentY
                        imageWidth: editViewport.paintedContentWidth
                        imageHeight: editViewport.paintedContentHeight
                        transformScale: editorPreview.transformScale
                        transformRotation: editorPreview.transformRotation
                        onScaleEdited: function(scale) { editorPreview.transformScale = scale }
                        onRotationEdited: function(rotation) { editorPreview.transformRotation = rotation }
                    }

                    LayerTransformOverlay {
                        id: photoLayerTransformOverlay
                        objectName: "photoLayerTransformOverlay"
                        anchors.fill: parent
                        readonly property var layerData: root.currentPhotoLayerData()
                        readonly property int layerIndex: root.normalizedActivePhotoLayerIndex()
                        active: root.page === "edit" &&
                                root.editTool !== "crop" &&
                                !editorPreview.compareMode &&
                                root.photoLayerHasDirectTransform(layerData)
                        locked: layerData.locked === true
                        imageX: editViewport.paintedContentX
                        imageY: editViewport.paintedContentY
                        imageWidth: editViewport.paintedContentWidth
                        imageHeight: editViewport.paintedContentHeight
                        canvasWidth: root.sourceCanvasWidthForLayers(layerData)
                        canvasHeight: root.sourceCanvasHeightForLayers(layerData)
                        layerX: Number(layerData.x || 0)
                        layerY: Number(layerData.y || 0)
                        layerWidth: Number(layerData.pixelWidth || 0)
                        layerHeight: Number(layerData.pixelHeight || 0)
                        layerScale: Number(layerData.scale || 1)
                        layerRotation: Number(layerData.rotation || 0)
                        z: 20
                        onTransformEdited: function(x, y, scale, rotation) {
                            if (layerIndex >= 0)
                                editorPreview.setPhotoLayerTransform(layerIndex, x, y, scale, rotation)
                        }
                    }

                    Rectangle {
                        id: pixelInspectorHud
                        objectName: "pixelInspectorHud"
                        readonly property var inspector: editorPreview.pixelInspector
                        visible: inspector && inspector.active === true && root.editTool !== "crop"
                        x: root.clamp(editViewport.pointerX + 14, 8, parent.width - width - 8)
                        y: root.clamp(editViewport.pointerY + 14, 8, parent.height - height - 8)
                        width: Math.max(238, pixelInspectorContent.implicitWidth + 18)
                        height: pixelInspectorContent.implicitHeight + 14
                        radius: 4
                        color: theme.alpha(theme.ink950, 0.88)
                        border.width: theme.chromeBorderWidth
                        border.color: theme.alpha(root.mint, 0.42)
                        z: 21

                        ColumnLayout {
                            id: pixelInspectorContent
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Label {
                                    objectName: "pixelInspectorCoordinateLabel"
                                    Layout.fillWidth: true
                                    text: "PX " + pixelInspectorHud.inspector.pixelX + ", " + pixelInspectorHud.inspector.pixelY
                                    color: root.mint
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                }
                                Label {
                                    text: pixelInspectorHud.inspector.hasBefore === true ? "BEFORE / AFTER" : "AFTER"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                }
                            }

                            Label {
                                objectName: "pixelInspectorBeforeLabel"
                                visible: pixelInspectorHud.inspector.hasBefore === true
                                Layout.fillWidth: true
                                text: "Before  " + root.pixelValueText(pixelInspectorHud.inspector.before)
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                objectName: "pixelInspectorAfterLabel"
                                Layout.fillWidth: true
                                text: "After   " + root.pixelValueText(pixelInspectorHud.inspector.after)
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                objectName: "pixelInspectorDeltaLabel"
                                Layout.fillWidth: true
                                text: root.pixelDeltaText(pixelInspectorHud.inspector)
                                color: pixelInspectorHud.inspector.hasBefore === true ? theme.accentCool400 : theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Rectangle {
                        objectName: "cropActiveCanvasBorder"
                        x: editViewport.paintedContentWidth > 1 ? editViewport.paintedContentX : 0
                        y: editViewport.paintedContentHeight > 1 ? editViewport.paintedContentY : 0
                        width: editViewport.paintedContentWidth > 1 ? editViewport.paintedContentWidth : parent.width
                        height: editViewport.paintedContentHeight > 1 ? editViewport.paintedContentHeight : parent.height
                        visible: root.editTool === "crop" && !editorPreview.compareMode
                        color: "transparent"
                        border.width: 2
                        border.color: root.mint
                        radius: 3
                        z: 21
                    }

                    Rectangle {
                        objectName: "cropToolHud"
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        width: cropToolControls.implicitWidth + 18
                        height: 36
                        radius: 3
                        visible: root.editTool === "crop" && !editorPreview.compareMode
                        color: theme.alpha(theme.ink950, 0.82)
                        border.width: theme.chromeBorderWidth
                        border.color: root.mint
                        z: 22

                        RowLayout {
                            id: cropToolControls
                            anchors.centerIn: parent
                            spacing: 6

                            Label {
                                objectName: "cropToolHeaderLabel"
                                text: root.cropToolMode === "transform" ? "CROP / TRANSFORM"
                                      : root.cropToolMode === "horizon" ? "CROP / HORIZON"
                                      : "CROP / BOX"
                                color: root.mint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0
                            }
                            MiniButton {
                                text: "Box"
                                iconName: "crop"
                                role: "segmented"
                                active: root.cropToolMode === "crop"
                                tooltip: root.complexToolTooltip("crop")
                                onClicked: root.setCropToolMode("crop")
                            }
                            MiniButton {
                                text: "Transform"
                                iconName: "geometry"
                                role: "segmented"
                                active: root.cropToolMode === "transform"
                                tooltip: root.complexToolTooltip("transform")
                                onClicked: root.setCropToolMode("transform")
                            }
                            MiniButton {
                                text: "Horizon"
                                iconName: "level"
                                role: "segmented"
                                active: root.cropToolMode === "horizon"
                                onClicked: root.setCropToolMode("horizon")
                            }
                            MiniButton {
                                text: "Done"
                                iconName: "check"
                                role: "primary"
                                onClicked: root.activateEditTool("adjust")
                            }
                        }
                    }

                    Rectangle {
                        objectName: "brushToolHud"
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        width: Math.min(parent.width - 24, brushToolControls.implicitWidth + 18)
                        height: 68
                        radius: 3
                        visible: root.brushToolReady() && !editorPreview.compareMode
                        color: theme.alpha(theme.ink950, 0.84)
                        border.width: theme.chromeBorderWidth
                        border.color: editorPreview.activeMaskOperation === "subtract" ? "#d98962" : root.mint
                        z: 23
                        clip: true

                        ColumnLayout {
                            id: brushToolControls
                            anchors.centerIn: parent
                            spacing: 6

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 6

                                Label {
                                    text: "Brush"
                                    color: editorPreview.activeMaskOperation === "subtract" ? "#d98962" : root.mint
                                    font.family: theme.fontUi
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 0
                                }
                                MiniButton {
                                    objectName: "brushHudAddButton"
                                    text: "Add"
                                    iconName: "brush-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskOperation === "add"
                                    accent: root.mint
                                    tooltip: "Paint into the active mask."
                                    onClicked: root.setBrushPaintMode("add")
                                }
                                MiniButton {
                                    objectName: "brushHudEraseButton"
                                    text: "Erase"
                                    iconName: "brush-subtract"
                                    role: "segmented"
                                    active: editorPreview.activeMaskOperation === "subtract"
                                    accent: "#d98962"
                                    tooltip: "Erase from the active mask."
                                    onClicked: root.setBrushPaintMode("subtract")
                                }
                                Label {
                                    objectName: "brushHudStatusLabel"
                                    text: root.brushHudStatusText()
                                    color: theme.textMain
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                                MiniButton {
                                    objectName: "brushHudDoneButton"
                                    text: "Done"
                                    iconName: "check"
                                    role: "primary"
                                    onClicked: {
                                        editorPreview.deselectActiveMask()
                                        root.activateEditTool("adjust")
                                    }
                                }
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 6

                                MiniButton {
                                    objectName: "brushHudSizeDownButton"
                                    square: true
                                    iconName: "minus"
                                    tooltip: "Decrease brush size."
                                    onClicked: root.adjustActiveBrushSize(-0.02)
                                }
                                MiniButton {
                                    objectName: "brushHudSizeUpButton"
                                    square: true
                                    iconName: "plus"
                                    tooltip: "Increase brush size."
                                    onClicked: root.adjustActiveBrushSize(0.02)
                                }
                                MiniButton {
                                    objectName: "brushHudFlowDownButton"
                                    text: "Flow -"
                                    role: "segmented"
                                    tooltip: "Lower brush flow."
                                    onClicked: root.adjustActiveBrushFlow(-0.05)
                                }
                                MiniButton {
                                    objectName: "brushHudFlowUpButton"
                                    text: "Flow +"
                                    role: "segmented"
                                    tooltip: "Raise brush flow."
                                    onClicked: root.adjustActiveBrushFlow(0.05)
                                }
                                MiniButton {
                                    objectName: "brushHudHardnessDownButton"
                                    text: "Soft"
                                    role: "segmented"
                                    tooltip: "Soften the brush edge."
                                    onClicked: root.adjustActiveBrushHardness(-0.08)
                                }
                                MiniButton {
                                    objectName: "brushHudHardnessUpButton"
                                    text: "Hard"
                                    role: "segmented"
                                    tooltip: "Harden the brush edge."
                                    onClicked: root.adjustActiveBrushHardness(0.08)
                                }
                            }
                        }
                    }

                    Rectangle {
                        objectName: "retouchToolHud"
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        width: Math.min(parent.width - 24, retouchToolControls.implicitWidth + 18)
                        height: 68
                        radius: 3
                        visible: editorPreview.hasActiveMask &&
                                 editorPreview.activeMaskToolRole === "retouch" &&
                                 !editorPreview.compareMode
                        color: theme.alpha(theme.ink950, 0.84)
                        border.width: theme.chromeBorderWidth
                        border.color: "#d98962"
                        z: 24
                        clip: true

                        ColumnLayout {
                            id: retouchToolControls
                            anchors.centerIn: parent
                            spacing: 6

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 6

                                Label {
                                    text: "Retouch"
                                    color: "#d98962"
                                    font.family: theme.fontUi
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 0
                                }
                                Label {
                                    objectName: "retouchHudStatusLabel"
                                    text: root.retouchHudStatusText()
                                    color: theme.textMain
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                                MiniButton {
                                    objectName: "retouchHudRemoveButton"
                                    text: "Remove"
                                    iconName: "erase-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "remove"
                                    accent: "#d98962"
                                    tooltip: "Paint an object removal area."
                                    onClicked: root.startRetouchTool("remove")
                                }
                                MiniButton {
                                    objectName: "retouchHudHealButton"
                                    text: "Heal"
                                    iconName: "brush-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "heal"
                                    accent: "#72cbb2"
                                    tooltip: "Blend blemishes into nearby texture."
                                    onClicked: root.startRetouchTool("heal")
                                }
                                MiniButton {
                                    objectName: "retouchHudPatchButton"
                                    text: "Patch"
                                    iconName: "brush-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "patch"
                                    accent: "#72cbb2"
                                    tooltip: "Patch a painted area from surrounding pixels."
                                    onClicked: root.startRetouchTool("patch")
                                }
                                MiniButton {
                                    objectName: "retouchHudCloneButton"
                                    text: "Clone"
                                    iconName: "brush-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "clone"
                                    accent: "#7cb5ff"
                                    tooltip: "Clone from a sampled source."
                                    onClicked: root.startRetouchTool("clone")
                                }
                                MiniButton {
                                    objectName: "retouchHudApplyButton"
                                    text: editorPreview.activeMaskCommitted ? "Applied" : "Apply"
                                    iconName: "check"
                                    role: "primary"
                                    enabled: !editorPreview.activeMaskCommitted
                                    onClicked: editorPreview.applyActiveRetouch()
                                }
                                MiniButton {
                                    objectName: "retouchHudDoneButton"
                                    text: "Done"
                                    iconName: "check"
                                    role: "primary"
                                    onClicked: {
                                        editorPreview.deselectActiveMask()
                                        root.activateEditTool("adjust")
                                    }
                                }
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 6

                                MiniButton {
                                    objectName: "retouchHudDustButton"
                                    text: "Dust"
                                    iconName: "erase-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "dust"
                                    accent: "#c2b36a"
                                    tooltip: "Stage dust spot cleanup."
                                    onClicked: root.startRetouchTool("dust")
                                }
                                MiniButton {
                                    objectName: "retouchHudRedEyeButton"
                                    text: "Red Eye"
                                    iconName: "erase-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "red-eye"
                                    accent: "#d36a5e"
                                    tooltip: "Stage red-eye correction."
                                    onClicked: root.startRetouchTool("red-eye")
                                }
                                MiniButton {
                                    objectName: "retouchHudSkinButton"
                                    text: "Skin"
                                    iconName: "brush-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "skin-cleanup"
                                    accent: "#d7a08c"
                                    tooltip: "Stage skin cleanup."
                                    onClicked: root.startRetouchTool("skin-cleanup")
                                }
                                MiniButton {
                                    objectName: "retouchHudSmoothButton"
                                    text: "Smooth"
                                    iconName: "brush-tool"
                                    role: "segmented"
                                    active: editorPreview.activeMaskRetouchMode === "frequency-smoothing"
                                    accent: "#c994d8"
                                    tooltip: "Stage frequency smoothing."
                                    onClicked: root.startRetouchTool("frequency-smoothing")
                                }
                                MiniButton {
                                    objectName: "retouchHudRegenerateButton"
                                    text: "Regenerate"
                                    role: "segmented"
                                    enabled: !editorPreview.activeMaskCommitted &&
                                             (editorPreview.activeMaskRetouchMode === "remove" ||
                                              editorPreview.activeMaskRetouchMode === "heal" ||
                                              editorPreview.activeMaskRetouchMode === "patch")
                                    tooltip: "Try another local inpaint seed."
                                    onClicked: editorPreview.regenerateInpaintSeed()
                                }
                                MiniButton {
                                    objectName: "retouchHudBeforeAfterButton"
                                    text: editorPreview.retouchBeforeAfterEnabled ? "Before" : "After"
                                    role: "segmented"
                                    active: editorPreview.retouchBeforeAfterEnabled
                                    tooltip: "Toggle retouch before and after."
                                    onClicked: editorPreview.retouchBeforeAfterEnabled = !editorPreview.retouchBeforeAfterEnabled
                                }
                            }
                        }
                    }

                    EmptyStateCard {
                        objectName: "editEmptyState"
                        anchors.centerIn: parent
                        width: Math.min(460, parent.width - 48)
                        height: 220
                        visible: root.editEmptyStateTitle().length > 0 && root.editTool !== "compare"
                        iconName: "media"
                        title: root.editEmptyStateTitle()
                        message: root.editEmptyStateMessage()
                        z: 18
                    }

                    Rectangle {
                        id: editZoomToolbar
                        objectName: "editZoomToolbar"
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 12
                        width: zoomControls.implicitWidth + 18
                        height: 36
                        radius: 3
                        color: theme.alpha(theme.ink950, 0.76)
                        border.width: theme.chromeBorderWidth
                        border.color: root.hairlineStrong
                        z: 20

                        RowLayout {
                            id: zoomControls
                            anchors.centerIn: parent
                            spacing: 6

                            MiniButton { objectName: "fitModeButton"; text: editViewport.fitMode === "fit" ? "Fit" : "Fill"; role: "segmented"; onClicked: editViewport.fitMode = editViewport.fitMode === "fit" ? "fill" : "fit" }
                            MiniButton { objectName: "zoomStepButton"; text: Math.round(editViewport.zoom * 100) + "%"; role: "segmented"; labelVisible: !editViewport.toolbarTight; onClicked: editViewport.setZoom(editViewport.zoom >= 4 ? 1 : editViewport.zoom + 0.25) }
                            MiniButton { objectName: "zoomOutButton"; square: true; iconName: "minus"; role: "icon"; onClicked: editViewport.zoomOut() }
                            MiniButton { objectName: "zoomInButton"; square: true; iconName: "plus"; role: "icon"; onClicked: editViewport.zoomIn() }
                            MiniButton { objectName: "loupeOverlayButton"; text: "Loupe"; iconName: "plus"; role: "segmented"; labelVisible: !editViewport.toolbarCompact; active: root.loupeEnabled; tooltip: root.complexToolTooltip("loupe"); onClicked: root.loupeEnabled = !root.loupeEnabled }
                            MiniButton { objectName: "focusOverlayButton"; text: "Focus"; iconName: "snap"; role: "segmented"; labelVisible: !editViewport.toolbarCompact; active: root.focusCheckOverlayEnabled; tooltip: root.complexToolTooltip("focus-check"); onClicked: root.focusCheckOverlayEnabled = !root.focusCheckOverlayEnabled }
                            MiniButton { objectName: "peakingOverlayButton"; text: "Peak"; iconName: "snap"; role: "segmented"; labelVisible: !editViewport.toolbarCompact; active: root.focusPeakingOverlayEnabled; tooltip: root.complexToolTooltip("focus-peaking"); onClicked: root.focusPeakingOverlayEnabled = !root.focusPeakingOverlayEnabled }
                            MiniButton { objectName: "clippingOverlayButton"; text: "Clips"; iconName: "scope"; role: "segmented"; labelVisible: !editViewport.toolbarCompact; active: root.clippingOverlayEnabled; tooltip: root.complexToolTooltip("clipping"); onClicked: root.clippingOverlayEnabled = !root.clippingOverlayEnabled }
                            MiniButton { objectName: "falseColorOverlayButton"; text: "False"; iconName: "scope"; role: "segmented"; labelVisible: !editViewport.toolbarCompact; active: root.falseColorOverlayEnabled; tooltip: root.complexToolTooltip("false-color"); onClicked: root.falseColorOverlayEnabled = !root.falseColorOverlayEnabled }
                            MiniButton { objectName: "gamutOverlayButton"; text: "Gamut"; iconName: "scope"; role: "segmented"; labelVisible: !editViewport.toolbarCompact; active: root.gamutOverlayEnabled; tooltip: root.complexToolTooltip("gamut-warning"); onClicked: root.gamutOverlayEnabled = !root.gamutOverlayEnabled }
                            MiniButton { objectName: "zoneMapOverlayButton"; text: "Zone"; iconName: "scope"; role: "segmented"; labelVisible: !editViewport.toolbarCompact; active: root.zoneMapOverlayEnabled; tooltip: root.complexToolTooltip("zone-map"); onClicked: root.zoneMapOverlayEnabled = !root.zoneMapOverlayEnabled }
                            MiniButton { objectName: "resetViewButton"; text: "Reset"; iconName: "reset"; role: "segmented"; labelVisible: !editViewport.toolbarTight; onClicked: editViewport.resetView() }
                        }
                    }

                    Rectangle {
                        id: editActionToolbar
                        objectName: "editActionToolbar"
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        anchors.topMargin: editViewport.toolbarStacked ? 54 : 12
                        width: editActionControls.implicitWidth + 18
                        height: 36
                        radius: 3
                        color: theme.alpha(theme.ink950, 0.76)
                        border.width: theme.chromeBorderWidth
                        border.color: root.hairlineStrong
                        z: 20

                        RowLayout {
                            id: editActionControls
                            anchors.centerIn: parent
                            spacing: 6

                            MiniButton { objectName: "compareModeButton"; text: "Compare"; iconName: "compare"; role: "segmented"; labelVisible: !editViewport.toolbarTight; active: root.editTool === "compare"; tooltip: root.complexToolTooltip("compare"); onClicked: root.activateEditTool("compare") }
                            MiniButton { objectName: "referenceImageButton"; text: editorPreview.hasReferenceImage ? "Ref Set" : "Ref"; iconName: "source"; role: "segmented"; labelVisible: !editViewport.toolbarTight; active: editorPreview.hasReferenceImage; tooltip: root.complexToolTooltip("reference"); onClicked: referenceImageDialog.open() }
                            MiniButton { objectName: "clearReferenceImageButton"; text: "Clear"; iconName: "reset"; role: "secondary"; labelVisible: !editViewport.toolbarTight; visible: editorPreview.hasReferenceImage; onClicked: editorPreview.clearReferenceImage() }
                            MiniButton { objectName: "cropModeButton"; text: root.editTool === "crop" ? "Done" : "Crop"; iconName: "crop"; role: "segmented"; active: root.editTool === "crop"; tooltip: root.complexToolTooltip("crop"); onClicked: root.activateEditTool(root.editTool === "crop" ? "adjust" : "crop") }
                            MiniButton { objectName: "contextPanelToggleButton"; text: "Ctx"; iconName: "menu"; role: "segmented"; labelVisible: !editViewport.toolbarTight; active: root.editContextPanelVisible && !root.editSidebarCollapsed; tooltip: root.complexToolTooltip("context-panel"); onClicked: { root.editContextPanelVisible = !root.editContextPanelVisible; if (root.editContextPanelVisible) root.editSidebarCollapsed = false } }
                            MiniButton { objectName: "inspectorPanelToggleButton"; text: "Insp"; iconName: "adjust"; role: "segmented"; labelVisible: !editViewport.toolbarTight; active: root.editInspectorPanelVisible; tooltip: root.complexToolTooltip("inspector-panel"); onClicked: root.editInspectorPanelVisible = !root.editInspectorPanelVisible }
                            MiniButton { objectName: "interfaceDensityButton"; text: root.compactMode ? "Pro" : "Compact"; iconName: "grid"; role: "segmented"; labelVisible: !editViewport.toolbarTight; active: root.compactMode; tooltip: root.complexToolTooltip("compact-mode"); onClicked: root.toggleInterfaceMode() }
                            MiniButton { objectName: "fullScreenEditButton"; text: root.fullScreenEditingEnabled ? "Exit" : "Full"; iconName: "media"; role: "segmented"; labelVisible: !editViewport.toolbarTight; active: root.fullScreenEditingEnabled; tooltip: root.complexToolTooltip("full-screen-edit"); onClicked: root.toggleFullScreenEditing() }
                        }
                    }
                }

                Rectangle {
                    visible: root.largeScopesMode && root.editInspectorTab === "scopes"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.panelColor
                    border.width: theme.chromeBorderWidth
                    border.color: root.hairline
                    radius: 3

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle { Layout.fillWidth: true; label: "Scopes" }
                            MiniButton { text: "RGB"; role: "segmented"; active: root.scopeShowRgb; tooltip: root.complexToolTooltip("scope-rgb"); onClicked: root.scopeShowRgb = !root.scopeShowRgb }
                            MiniButton { text: "Luma"; role: "segmented"; active: root.scopeShowLuma; tooltip: root.complexToolTooltip("scope-luma"); onClicked: root.scopeShowLuma = !root.scopeShowLuma }
                            MiniButton { text: "Density"; role: "segmented"; active: root.scopeShowDensity; tooltip: root.complexToolTooltip("scope-density"); onClicked: root.scopeShowDensity = !root.scopeShowDensity }
                            MiniButton { text: "Trace"; role: "segmented"; active: root.scopeShowTrace; tooltip: root.complexToolTooltip("scope-trace"); onClicked: root.scopeShowTrace = !root.scopeShowTrace }
                            MiniButton { text: root.scopeLayoutPreset; role: "segmented"; onClicked: root.setScopeLayoutPreset(root.scopeLayoutPreset === "Stacked" ? "Quad" : root.scopeLayoutPreset === "Quad" ? "Tall" : "Stacked") }
                            MiniButton { text: root.scopeSamplingMode; role: "segmented"; onClicked: root.setScopeSamplingMode(root.scopeSamplingMode === "Fast" ? "Balanced" : root.scopeSamplingMode === "Balanced" ? "Precise" : "Fast") }
                            MiniButton { text: "Image"; role: "secondary"; onClicked: root.largeScopesMode = false }
                        }

                        ScopeGrid {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            large: true
                        }
                    }
                }

                RowLayout {
                    objectName: "compareWorkspaceRow"
                    visible: root.editTool === "compare"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8

                    Panel {
                        objectName: "beforeAfterSplitPanel"
                        visible: root.compareViewMode !== "side-by-side"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: visible ? 1 : 0
                        title: "Before / After"
                        border.color: root.compareViewMode === "split" ? root.mint : root.hairline

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                MiniButton {
                                    text: "Split"
                                    iconName: "split-view"
                                    role: "segmented"
                                    active: root.compareViewMode === "split"
                                    onClicked: root.compareViewMode = "split"
                                }

                                MiniButton {
                                    text: "Side"
                                    iconName: "compare"
                                    role: "segmented"
                                    active: root.compareViewMode === "side-by-side"
                                    onClicked: root.compareViewMode = "side-by-side"
                                }

                                MiniButton {
                                    text: "Wipe"
                                    iconName: "wipe"
                                    role: "segmented"
                                    active: root.compareViewMode === "wipe"
                                    onClicked: root.compareViewMode = "wipe"
                                }

                                MiniButton {
                                    text: "Sync"
                                    iconName: "link"
                                    role: "segmented"
                                    active: root.syncEnabled
                                    onClicked: root.syncEnabled = !root.syncEnabled
                                }

                                Repeater {
                                    model: [
                                        { label: "Tone", group: "tone" },
                                        { label: "Color", group: "color" },
                                        { label: "Detail", group: "detail" },
                                        { label: "Masks", group: "masks" }
                                    ]

                                    MiniButton {
                                        required property var modelData
                                        text: modelData.label
                                        role: "segmented"
                                        active: root.compareEditGroupActive(modelData.group)
                                        onClicked: root.toggleCompareEditGroup(modelData.group)
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Item {
                                id: beforeAfterSplitView
                                objectName: "beforeAfterSplitView"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    visible: root.compareViewMode === "split"

                                    Image {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        source: root.displayImageSource(-1)
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: false
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 1
                                        Layout.fillHeight: true
                                        color: theme.alpha(root.lineBright, 0.65)
                                    }

                                    Image {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        source: root.displayImageSource(0)
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: false
                                    }
                                }

                                Image {
                                    id: beforeAfterBase
                                    anchors.fill: parent
                                    source: root.displayImageSource(-1)
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                    visible: root.compareViewMode === "wipe"
                                }

                                Item {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: parent.width * root.compareSplitPosition
                                    clip: true
                                    visible: root.compareViewMode === "wipe"

                                    Image {
                                        width: beforeAfterSplitView.width
                                        height: beforeAfterSplitView.height
                                        source: root.displayImageSource(0)
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: false
                                    }
                                }

                                Image {
                                    anchors.fill: parent
                                    source: root.displayImageSource(0)
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                    visible: root.compareViewMode === "side-by-side"
                                }

                                Rectangle {
                                    id: compareSplitHandle
                                    objectName: "beforeAfterWipeHandle"
                                    visible: root.compareViewMode === "wipe"
                                    x: Math.round(beforeAfterSplitView.width * root.compareSplitPosition) - width / 2
                                    y: 0
                                    width: 2
                                    height: beforeAfterSplitView.height
                                    color: theme.alpha(root.mint, 0.9)

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 18
                                        height: 34
                                        radius: 3
                                        color: theme.alpha(root.panelColor, 0.88)
                                        border.width: 1
                                        border.color: theme.alpha(root.mint, 0.72)
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    objectName: "beforeAfterWipeView"
                                    enabled: root.compareViewMode === "wipe"
                                    cursorShape: Qt.SplitHCursor
                                    onPressed: function(mouse) {
                                        root.compareSplitPosition = root.clamp(mouse.x / Math.max(1, beforeAfterSplitView.width), 0.05, 0.95)
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (!pressed)
                                            return
                                        root.compareSplitPosition = root.clamp(mouse.x / Math.max(1, beforeAfterSplitView.width), 0.05, 0.95)
                                    }
                                }
                            }
                        }
                    }

                    Repeater {
                        id: compareViewportRepeater
                        model: root.compareViewMode === "side-by-side" ? 2 : 0
                        Component.onCompleted: root.compareViewportCount = count
                        onCountChanged: root.compareViewportCount = count

                        Panel {
                            id: comparePanel
                            objectName: "comparePanel" + index
                            required property int index
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            title: root.comparePanelTitle(index)
                            border.color: root.selectedCompareIndex === index ? root.mint : root.hairline

                            ImageViewport {
                                objectName: "compareImageViewport" + comparePanel.index
                                anchors.fill: parent
                                anchors.topMargin: 28
                                framed: false
                                source: root.displayImageSource(comparePanel.index - 1)
                                syncViewport: root.syncEnabled
                                loupeEnabled: true
                                focusCheckOverlayEnabled: true
                                focusPeakingOverlayEnabled: true
                                clippingOverlayEnabled: true
                                falseColorOverlayEnabled: true
                                gamutOverlayEnabled: true
                                zoneMapOverlayEnabled: true
                            }

                            MouseArea { anchors.fill: parent; onClicked: root.selectedCompareIndex = index }
                        }
                    }
                }

                Rectangle {
                    visible: root.editingChromeVisible
                    Layout.fillWidth: true
                    Layout.preferredHeight: 86
                    color: root.panelColor
                    border.width: theme.chromeBorderWidth
                    border.color: root.hairline
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            orientation: ListView.Horizontal
                            spacing: 10
                            clip: true
                            model: catalogAssets
                            delegate: ThumbnailCard {
                                required property int index
                                width: 118
                                height: 66
                                assetIndex: index
                                filmstrip: true
                                onActivated: root.selectAsset(index)
                                onDoubleActivated: root.selectAsset(index)
                            }
                        }
                    }
                }
            }

            Panel {
                objectName: "editInspectorPanel"
                visible: root.editingChromeVisible && root.editInspectorPanelVisible
                Layout.preferredWidth: visible ? root.editInspectorPanelWidth : 0
                Layout.fillHeight: true
                title: root.inspectorTabTitle(root.editInspectorTab)

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        objectName: "editInspectorTabRow"
                        Layout.fillWidth: true
                        MiniButton { objectName: "adjustInspectorTabButton"; Layout.fillWidth: true; text: "Adjust"; role: "segmented"; active: root.editInspectorTab === "adjust"; onClicked: root.editInspectorTab = "adjust" }
                        MiniButton { objectName: "scopesInspectorTabButton"; Layout.fillWidth: true; text: "Scopes"; role: "segmented"; active: root.editInspectorTab === "scopes"; onClicked: root.editInspectorTab = "scopes" }
                        MiniButton { objectName: "detailInspectorTabButton"; Layout.fillWidth: true; text: "Detail"; role: "segmented"; active: root.editInspectorTab === "detail"; onClicked: root.editInspectorTab = "detail" }
                        MiniButton { objectName: "infoInspectorTabButton"; Layout.fillWidth: true; text: "Info"; role: "segmented"; active: root.editInspectorTab === "info"; onClicked: root.editInspectorTab = "info" }
                    }

                    ScrollView {
                        id: editInspectorScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: editInspectorScroll.availableWidth
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.editInspectorTab === "adjust"
                                spacing: 12

                                HistogramDisplay {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    redData: editorPreview.histogramRed
                                    greenData: editorPreview.histogramGreen
                                    blueData: editorPreview.histogramBlue
                                    luminanceData: editorPreview.histogramLuminance
                                }

                                InspectorGroup {
                                    title: "Layers"

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            Layout.fillWidth: true
                                            text: editorPreview.photoLayerSoloActive ? "ISOLATION ACTIVE" : editorPreview.photoLayerCount + " PHOTO LAYERS"
                                            color: editorPreview.photoLayerSoloActive ? root.mint : root.dimText
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        MiniButton {
                                            objectName: "photoLayerSoloButton"
                                            visible: editorPreview.photoLayerCount > 0
                                            text: editorPreview.photoLayerSoloActive ? "Isolated" : "Isolate"
                                            role: "segmented"
                                            active: editorPreview.photoLayerSoloActive
                                            tooltip: editorPreview.photoLayerSoloActive ? "Clear layer isolation." : "Show only the top layer."
                                            onClicked: {
                                                if (editorPreview.photoLayerSoloActive)
                                                    editorPreview.clearPhotoLayerSolo()
                                                else
                                                    editorPreview.isolatePhotoLayer(0)
                                            }
                                        }

                                        MiniButton {
                                            objectName: "photoLayerClearSoloButton"
                                            visible: editorPreview.photoLayerSoloActive
                                            text: "Clear Isolate"
                                            role: "segmented"
                                            active: true
                                            tooltip: "Show all visible layers again."
                                            onClicked: editorPreview.clearPhotoLayerSolo()
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        MiniButton {
                                            Layout.fillWidth: true
                                            text: "Add Current"
                                            iconName: "adjust"
                                            onClicked: editorPreview.addAdjustmentLayer("")
                                        }
                                        MiniButton {
                                            Layout.fillWidth: true
                                            text: "Fill"
                                            onClicked: editorPreview.addFillLayer("#808080", "")
                                        }
                                        MiniButton {
                                            Layout.fillWidth: true
                                            text: "Text"
                                            onClicked: editorPreview.addTextLayer("Text", "")
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        readonly property int stateCount: editorPreview.photoLayerVisibilityStateCount
                                        readonly property var latestState: stateCount > 0 ? editorPreview.photoLayerVisibilityStates[stateCount - 1] : ({})

                                        Label {
                                            objectName: "photoLayerVisibilityStateLabel"
                                            Layout.fillWidth: true
                                            text: parent.stateCount > 0
                                                  ? String(parent.latestState.name || "Visibility") + " · " + String(parent.latestState.visibilitySummary || "")
                                                  : "No visibility states"
                                            color: parent.stateCount > 0 ? theme.textMain : root.dimText
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }

                                        MiniButton {
                                            objectName: "saveLayerVisibilityStateButton"
                                            text: "Save Vis"
                                            role: "segmented"
                                            enabled: editorPreview.photoLayerCount > 0
                                            tooltip: "Save current layer visibility and isolation."
                                            onClicked: editorPreview.savePhotoLayerVisibilityState("")
                                        }

                                        MiniButton {
                                            objectName: "applyLayerVisibilityStateButton"
                                            text: "Apply"
                                            role: "segmented"
                                            enabled: editorPreview.photoLayerVisibilityStateCount > 0
                                            tooltip: "Restore the latest saved layer visibility state."
                                            onClicked: editorPreview.applyPhotoLayerVisibilityState(editorPreview.photoLayerVisibilityStateCount - 1)
                                        }

                                        MiniButton {
                                            objectName: "deleteLayerVisibilityStateButton"
                                            text: "Delete"
                                            role: "segmented"
                                            enabled: editorPreview.photoLayerVisibilityStateCount > 0
                                            tooltip: "Delete the latest saved layer visibility state."
                                            onClicked: editorPreview.deletePhotoLayerVisibilityState(editorPreview.photoLayerVisibilityStateCount - 1)
                                        }
                                    }

                                    Repeater {
                                        model: editorPreview.photoLayerCount

                                        delegate: Rectangle {
                                            required property int index
                                            readonly property var layerData: editorPreview.photoLayers[index] || ({})
                                            readonly property int layerIndex: Number(layerData.flatIndex !== undefined ? layerData.flatIndex : index)
                                            readonly property bool layerLocked: layerData.locked === true
                                            readonly property real layerOpacity: Number(layerData.opacity !== undefined ? layerData.opacity : 1.0)
                                            readonly property int layerOpacityPercent: Number(layerData.opacityPercent !== undefined ? layerData.opacityPercent : 100)

                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 82
                                            radius: 4
                                            color: layerData.enabled === false ? theme.alpha(root.panelColor, 0.42) : theme.alpha(root.mint, 0.055)
                                            border.width: theme.chromeBorderWidth
                                            border.color: layerData.solo === true ? theme.alpha(root.mint, 0.78)
                                                          : layerIndex === root.normalizedActivePhotoLayerIndex() ? theme.alpha(root.mint, 0.66)
                                                          : layerData.locked === true ? theme.alpha(root.dimText, 0.52)
                                                          : root.hairline

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                spacing: 7

                                                Rectangle {
                                                    objectName: "photoLayerThumbnail"
                                                    Layout.preferredWidth: 40
                                                    Layout.preferredHeight: 40
                                                    radius: 3
                                                    color: layerData.kind === "fill" ? String(layerData.fillColor || "#808080")
                                                           : layerData.kind === "text" ? theme.alpha(root.mint, 0.16)
                                                           : layerData.kind === "group" ? theme.alpha(root.dimText, 0.18)
                                                           : theme.alpha(theme.accentCool400, 0.14)
                                                    border.width: 1
                                                    border.color: theme.alpha(theme.textMain, 0.24)

                                                    Label {
                                                        anchors.centerIn: parent
                                                        text: layerData.kind === "text" ? "T"
                                                              : layerData.kind === "fill" ? ""
                                                              : layerData.kind === "group" ? "G"
                                                              : layerData.kind === "source" ? "S"
                                                              : "A"
                                                        color: theme.textMain
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 12
                                                        font.weight: Font.DemiBold
                                                    }

                                                    Rectangle {
                                                        objectName: "photoLayerMaskBadge"
                                                        visible: layerData.hasLayerMask === true
                                                        anchors.left: parent.left
                                                        anchors.right: parent.right
                                                        anchors.bottom: parent.bottom
                                                        height: 13
                                                        radius: 2
                                                        color: theme.alpha(root.mint, 0.82)

                                                        Label {
                                                            anchors.centerIn: parent
                                                            text: "MASK"
                                                            color: "#06100E"
                                                            font.family: theme.fontMono
                                                            font.pixelSize: 7
                                                            font.weight: Font.DemiBold
                                                        }
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 4

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: String(layerData.name || "Layer")
                                                        color: layerData.enabled === false ? root.dimText : theme.textMain
                                                        font.family: theme.fontUi
                                                        font.pixelSize: 11
                                                        font.weight: Font.DemiBold
                                                        elide: Text.ElideRight
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: String(layerData.layerAuditLabel || "")
                                                        color: root.dimText
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 8
                                                        elide: Text.ElideRight
                                                    }

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4

                                                        Label {
                                                            Layout.fillWidth: true
                                                            visible: String(layerData.dependencyLabel || "").length > 0
                                                            text: String(layerData.dependencyLabel || "")
                                                            color: root.mint
                                                            font.family: theme.fontUi
                                                            font.pixelSize: 9
                                                            elide: Text.ElideRight
                                                        }

                                                        Label {
                                                            objectName: "photoLayerOpacityLabel"
                                                            text: layerOpacityPercent + "%"
                                                            color: theme.textMain
                                                            font.family: theme.fontMono
                                                            font.pixelSize: 9
                                                            font.weight: Font.DemiBold
                                                        }
                                                    }

                                                    Flow {
                                                        Layout.fillWidth: true
                                                        spacing: 4

                                                        MiniButton {
                                                            objectName: "photoLayerOpacityDownButton"
                                                            width: 30
                                                            implicitWidth: 30
                                                            text: "-"
                                                            role: "segmented"
                                                            enabled: !layerLocked && layerOpacity > 0.0
                                                            tooltip: "Lower layer opacity."
                                                            onClicked: root.adjustPhotoLayerOpacity(layerIndex, layerData.opacity, -0.1)
                                                        }

                                                        MiniButton {
                                                            objectName: "photoLayerOpacityUpButton"
                                                            width: 30
                                                            implicitWidth: 30
                                                            text: "+"
                                                            role: "segmented"
                                                            enabled: !layerLocked && layerOpacity < 1.0
                                                            tooltip: "Raise layer opacity."
                                                            onClicked: root.adjustPhotoLayerOpacity(layerIndex, layerData.opacity, 0.1)
                                                        }

                                                        MiniButton {
                                                            objectName: "photoLayerBlendModeButton"
                                                            width: 86
                                                            implicitWidth: 86
                                                            text: root.photoLayerBlendLabel(layerData.blendMode)
                                                            role: "segmented"
                                                            active: String(layerData.blendMode || "normal") !== "normal"
                                                            enabled: !layerLocked
                                                            tooltip: "Cycle layer blend mode."
                                                            onClicked: root.cyclePhotoLayerBlendMode(layerIndex, layerData.blendMode)
                                                        }

                                                        MiniButton {
                                                            objectName: "photoLayerClippingMaskButton"
                                                            width: 46
                                                            implicitWidth: 46
                                                            text: "Clip"
                                                            role: "segmented"
                                                            active: layerData.clippingMask === true
                                                            enabled: !layerLocked && layerData.kind !== "group" && layerIndex < editorPreview.photoLayerCount - 1
                                                            tooltip: layerData.clippingMask === true ? "Release clipping mask." : "Clip this layer to the layer below."
                                                            onClicked: editorPreview.setPhotoLayerClippingMask(layerIndex, !(layerData.clippingMask === true))
                                                        }

                                                        MiniButton {
                                                            objectName: "photoLayerMaskButton"
                                                            width: 52
                                                            implicitWidth: 52
                                                            text: layerData.hasLayerMask === true ? "Mask" : "+Mask"
                                                            role: "segmented"
                                                            active: layerData.hasLayerMask === true
                                                            enabled: !layerLocked
                                                            tooltip: layerData.hasLayerMask === true ? "Remove this layer mask." : "Add a layer mask."
                                                            onClicked: editorPreview.setPhotoLayerMaskEnabled(layerIndex, !(layerData.hasLayerMask === true))
                                                        }

                                                        MiniButton {
                                                            objectName: "photoLayerTransformButton"
                                                            width: 58
                                                            implicitWidth: 58
                                                            text: "Xform"
                                                            role: "segmented"
                                                            active: layerIndex === root.normalizedActivePhotoLayerIndex()
                                                            enabled: root.photoLayerHasDirectTransform(layerData)
                                                            tooltip: active ? "This layer has direct canvas transform handles." : "Show transform handles for this layer."
                                                            onClicked: root.setActivePhotoLayer(layerIndex)
                                                        }

                                                        MiniButton {
                                                            objectName: "photoLayerVisibilityButton"
                                                            width: 42
                                                            implicitWidth: 42
                                                            text: layerData.enabled === false ? "Off" : "On"
                                                            role: "segmented"
                                                            active: layerData.enabled !== false
                                                            tooltip: "Toggle layer visibility."
                                                            onClicked: editorPreview.setPhotoLayerEnabled(layerIndex, !(layerData.enabled !== false))
                                                        }

                                                        MiniButton {
                                                            objectName: "photoLayerRowSoloButton"
                                                            width: 68
                                                            implicitWidth: 68
                                                            text: layerData.solo === true ? "Isolated" : "Isolate"
                                                            role: "segmented"
                                                            active: layerData.solo === true
                                                            enabled: !layerLocked || layerData.solo === true
                                                            tooltip: layerData.solo === true ? "Clear layer isolation." : "Show only this layer."
                                                            onClicked: {
                                                                if (layerData.solo === true)
                                                                    editorPreview.clearPhotoLayerSolo()
                                                                else
                                                                    editorPreview.isolatePhotoLayer(layerIndex)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: editorPreview.photoLayerCount === 0
                                        text: "No photo layers"
                                        color: root.dimText
                                        font.family: theme.fontUi
                                        font.pixelSize: 11
                                    }
                                }

                                InspectorGroup {
                                    objectName: "whiteBalanceInspectorGroup"
                                    title: "White Balance"
                                    resetGroup: "whiteBalance"
                                    resetObjectName: "whiteBalancePanelResetButton"
                                    AdjustSlider { objectName: "temperatureSlider"; label: "Temp"; valueInputMode: "kelvin"; value: editorPreview.temperature; valueText: root.whiteBalanceKelvin(editorPreview.temperature) + "K"; trackColors: ["#4FB5FF", "#F2C35B"]; onValueDragged: function(value) { editorPreview.temperature = value } }
                                    AdjustSlider { objectName: "tintSlider"; label: "Tint"; value: editorPreview.tint; valueText: root.signedPercent(editorPreview.tint); trackColors: ["#49B86B", "#C85AC1"]; onValueDragged: function(value) { editorPreview.tint = value } }
                                    MiniButton { Layout.fillWidth: true; text: "Sample White Balance"; iconName: "brush-tool"; role: "secondary"; onClicked: editorPreview.setWhiteBalanceSampling(true) }
                                }

                                InspectorGroup {
                                    objectName: "lightInspectorGroup"
                                    title: "Light"
                                    resetGroup: "light"
                                    resetObjectName: "lightPanelResetButton"
                                    CollapsibleGroup {
                                        objectName: "lightBasicToneGroup"
                                        title: "Basic Tone"
                                        expanded: true
                                        onToggled: expanded = !expanded
                                        AdjustSlider { objectName: "exposureSlider"; label: "Exposure"; from: -2; to: 2; stepSize: 0.01; value: editorPreview.exposure; valueText: root.exposureText(editorPreview.exposure); onValueDragged: function(value) { editorPreview.exposure = value } }
                                        AdjustSlider { objectName: "brightnessSlider"; label: "Brightness"; value: editorPreview.brightness; valueText: root.signedPercent(editorPreview.brightness); onValueDragged: function(value) { editorPreview.brightness = value } }
                                        AdjustSlider { label: "Contrast"; value: editorPreview.contrast; valueText: root.signedPercent(editorPreview.contrast); onValueDragged: function(value) { editorPreview.contrast = value } }
                                        AdjustSlider { label: "Highlights"; value: editorPreview.highlights; valueText: root.signedPercent(editorPreview.highlights); onValueDragged: function(value) { editorPreview.highlights = value } }
                                        AdjustSlider { label: "Shadows"; value: editorPreview.shadows; valueText: root.signedPercent(editorPreview.shadows); onValueDragged: function(value) { editorPreview.shadows = value } }
                                        AdjustSlider { label: "Whites"; value: editorPreview.whites; valueText: root.signedPercent(editorPreview.whites); onValueDragged: function(value) { editorPreview.whites = value } }
                                        AdjustSlider { label: "Blacks"; value: editorPreview.blacks; valueText: root.signedPercent(editorPreview.blacks); onValueDragged: function(value) { editorPreview.blacks = value } }
                                    }
                                    CollapsibleGroup {
                                        objectName: "lightAdvancedToneGroup"
                                        title: "Advanced Tone"
                                        expanded: root.lightAdvancedToneExpanded
                                        onToggled: root.lightAdvancedToneExpanded = !root.lightAdvancedToneExpanded
                                        AdjustSlider { label: "HDR Local Tone"; from: 0; to: 100; value: editorPreview.hdrLocalToneMapping; valueText: Math.round(editorPreview.hdrLocalToneMapping).toString(); onValueDragged: function(value) { editorPreview.hdrLocalToneMapping = value } }
                                        AdjustSlider { label: "Filmic Shoulder"; value: editorPreview.filmicShoulder; valueText: root.signedPercent(editorPreview.filmicShoulder); onValueDragged: function(value) { editorPreview.filmicShoulder = value } }
                                        AdjustSlider { label: "Filmic Toe"; value: editorPreview.filmicToe; valueText: root.signedPercent(editorPreview.filmicToe); onValueDragged: function(value) { editorPreview.filmicToe = value } }
                                        AdjustSlider { label: "Adaptive Contrast"; value: editorPreview.adaptiveContrast; valueText: root.signedPercent(editorPreview.adaptiveContrast); onValueDragged: function(value) { editorPreview.adaptiveContrast = value } }
                                    }
                                    CollapsibleGroup {
                                        objectName: "lightLevelsGroup"
                                        title: "Levels"
                                        expanded: root.lightLevelsExpanded
                                        onToggled: root.lightLevelsExpanded = !root.lightLevelsExpanded
                                        AdjustSlider { label: "Black Point"; from: 0; to: 100; value: editorPreview.levelsBlackPoint; valueText: Math.round(editorPreview.levelsBlackPoint).toString(); onValueDragged: function(value) { editorPreview.levelsBlackPoint = value } }
                                        AdjustSlider { label: "Gamma"; value: editorPreview.levelsGamma; valueText: root.signedPercent(editorPreview.levelsGamma); onValueDragged: function(value) { editorPreview.levelsGamma = value } }
                                        AdjustSlider { label: "White Point"; from: 0; to: 100; defaultValue: 100; value: editorPreview.levelsWhitePoint; valueText: Math.round(editorPreview.levelsWhitePoint).toString(); onValueDragged: function(value) { editorPreview.levelsWhitePoint = value } }
                                    }
                                    CollapsibleGroup {
                                        objectName: "lightZoneMappingGroup"
                                        title: "Zone Mapping"
                                        expanded: root.lightZonesExpanded
                                        onToggled: root.lightZonesExpanded = !root.lightZonesExpanded
                                        AdjustSlider { label: "Deep Shadows"; value: editorPreview.zoneDeepShadows; valueText: root.signedPercent(editorPreview.zoneDeepShadows); onValueDragged: function(value) { editorPreview.zoneDeepShadows = value } }
                                        AdjustSlider { label: "Shadows"; value: editorPreview.zoneShadows; valueText: root.signedPercent(editorPreview.zoneShadows); onValueDragged: function(value) { editorPreview.zoneShadows = value } }
                                        AdjustSlider { label: "Midtones"; value: editorPreview.zoneMidtones; valueText: root.signedPercent(editorPreview.zoneMidtones); onValueDragged: function(value) { editorPreview.zoneMidtones = value } }
                                        AdjustSlider { label: "Lights"; value: editorPreview.zoneLights; valueText: root.signedPercent(editorPreview.zoneLights); onValueDragged: function(value) { editorPreview.zoneLights = value } }
                                        AdjustSlider { label: "Highlights"; value: editorPreview.zoneHighlights; valueText: root.signedPercent(editorPreview.zoneHighlights); onValueDragged: function(value) { editorPreview.zoneHighlights = value } }
                                    }
                                }

                                InspectorGroup {
                                    objectName: "toneCurveInspectorGroup"
                                    title: "Tone Curve"
                                    ToneCurveEditor {
                                        objectName: "studioToneCurveEditor"
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 214
                                        compactMode: false
                                        accent: root.mint
                                        selectedChannel: editorPreview.curveChannel
                                        curveMode: editorPreview.activeCurveMode
                                        pointsModel: editorPreview.activeCurvePoints
                                        presetNames: editorPreview.curvePresetNames
                                        histogramRedData: editorPreview.histogramRed
                                        histogramGreenData: editorPreview.histogramGreen
                                        histogramBlueData: editorPreview.histogramBlue
                                        histogramLuminanceData: editorPreview.histogramLuminance
                                        histogramLogScale: root.scopeLogHistogram
                                        presetPreviewProvider: function(preset) { return editorPreview.curvePresetPreviewPoints(preset) }
                                        onChannelSelected: function(channel) { editorPreview.curveChannel = channel }
                                        onPointsEdited: function(points) { editorPreview.activeCurvePoints = points }
                                        onPresetSelected: function(preset) { editorPreview.applyCurvePreset(preset) }
                                    }
                                    AdjustSlider { label: "Shadows"; value: editorPreview.curveShadows; valueText: root.signedPercent(editorPreview.curveShadows); onValueDragged: function(value) { editorPreview.curveShadows = value } }
                                    AdjustSlider { label: "Darks"; value: editorPreview.curveDarks; valueText: root.signedPercent(editorPreview.curveDarks); onValueDragged: function(value) { editorPreview.curveDarks = value } }
                                    AdjustSlider { label: "Lights"; value: editorPreview.curveLights; valueText: root.signedPercent(editorPreview.curveLights); onValueDragged: function(value) { editorPreview.curveLights = value } }
                                    AdjustSlider { label: "Highlights"; value: editorPreview.curveHighlights; valueText: root.signedPercent(editorPreview.curveHighlights); onValueDragged: function(value) { editorPreview.curveHighlights = value } }
                                }

                                InspectorGroup {
                                    objectName: "colorInspectorGroup"
                                    title: "Color"
                                    resetGroup: "color"
                                    resetObjectName: "colorPanelResetButton"
                                    AdjustSlider { label: "Vibrance"; value: editorPreview.vibrance; valueText: root.signedPercent(editorPreview.vibrance); onValueDragged: function(value) { editorPreview.vibrance = value } }
                                    AdjustSlider { objectName: "saturationSlider"; label: "Saturation"; value: editorPreview.saturation; valueText: root.signedPercent(editorPreview.saturation); onValueDragged: function(value) { editorPreview.saturation = value } }
                                }

                                InspectorGroup {
                                    objectName: "presenceInspectorGroup"
                                    title: "Presence"
                                    resetGroup: "presence"
                                    resetObjectName: "presencePanelResetButton"
                                    AdjustSlider { label: "Texture"; value: editorPreview.texture; valueText: root.signedPercent(editorPreview.texture); onValueDragged: function(value) { editorPreview.texture = value } }
                                    AdjustSlider { label: "Clarity"; value: editorPreview.clarity; valueText: root.signedPercent(editorPreview.clarity); onValueDragged: function(value) { editorPreview.clarity = value } }
                                    AdjustSlider { label: "Dehaze"; value: editorPreview.dehaze; valueText: root.signedPercent(editorPreview.dehaze); onValueDragged: function(value) { editorPreview.dehaze = value } }
                                }

                                InspectorGroup {
                                    objectName: "rawInspectorGroup"
                                    title: "Raw"
                                    resetGroup: "raw"
                                    resetObjectName: "rawPanelResetButton"
                                    visible: editorPreview.rawControlsAvailable
                                    AdjustSlider { label: "Raw Exposure"; from: -2; to: 2; stepSize: 0.01; value: editorPreview.rawExposure; valueText: root.exposureText(editorPreview.rawExposure); onValueDragged: function(value) { editorPreview.rawExposure = value } }
                                    AdjustSlider { label: "Raw Boost"; value: editorPreview.rawBoost; valueText: root.signedPercent(editorPreview.rawBoost); onValueDragged: function(value) { editorPreview.rawBoost = value } }
                                    AdjustSlider { label: "Shadow Boost"; value: editorPreview.rawShadowBoost; valueText: root.signedPercent(editorPreview.rawShadowBoost); onValueDragged: function(value) { editorPreview.rawShadowBoost = value } }
                                    AdjustSlider { label: "Raw Detail"; value: editorPreview.rawDetail; valueText: root.signedPercent(editorPreview.rawDetail); onValueDragged: function(value) { editorPreview.rawDetail = value } }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        MiniButton { Layout.fillWidth: true; text: "Highlight Recovery"; role: "segmented"; active: editorPreview.rawHighlightRecovery; onClicked: editorPreview.rawHighlightRecovery = !editorPreview.rawHighlightRecovery }
                                        MiniButton { Layout.fillWidth: true; text: "Lens Correction"; role: "segmented"; active: editorPreview.rawLensCorrection; onClicked: editorPreview.rawLensCorrection = !editorPreview.rawLensCorrection }
                                    }
                                }

                                InspectorGroup {
                                    objectName: "advancedColorInspectorGroup"
                                    title: "Advanced Color"
                                    CollapsibleGroup {
                                    objectName: "colorMixGroup"
                                    title: "Color Mix"
                                    expanded: root.colorMixExpanded
                                    onToggled: root.colorMixExpanded = !root.colorMixExpanded
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Repeater {
                                            model: ["HSL", "HSV", "LCH", "OKLab"]
                                            MiniButton {
                                                required property string modelData
                                                Layout.fillWidth: true
                                                text: modelData
                                                role: "segmented"
                                                active: editorPreview.colorMixerModel === modelData
                                                onClicked: editorPreview.colorMixerModel = modelData
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Repeater {
                                            model: ["Reds", "Oranges", "Yellows", "Greens", "Aquas", "Blues", "Purples", "Magentas"]
                                            MiniButton {
                                                required property string modelData
                                                Layout.fillWidth: true
                                                text: modelData.slice(0, 1)
                                                role: "segmented"
                                                active: editorPreview.hslChannel === modelData
                                                onClicked: editorPreview.hslChannel = modelData
                                            }
                                        }
                                    }
                                    AdjustSlider { label: "Hue"; value: editorPreview.hueAdjustment; valueText: root.signedPercent(editorPreview.hueAdjustment); onValueDragged: function(value) { editorPreview.hueAdjustment = value } }
                                    AdjustSlider { label: "Saturation"; value: editorPreview.saturationAdjustment; valueText: root.signedPercent(editorPreview.saturationAdjustment); onValueDragged: function(value) { editorPreview.saturationAdjustment = value } }
                                    AdjustSlider { label: "Luminance"; value: editorPreview.luminanceAdjustment; valueText: root.signedPercent(editorPreview.luminanceAdjustment); onValueDragged: function(value) { editorPreview.luminanceAdjustment = value } }
                                    AdjustSlider { label: "Cyan"; value: editorPreview.selectiveCyan; valueText: root.signedPercent(editorPreview.selectiveCyan); onValueDragged: function(value) { editorPreview.selectiveCyan = value } }
                                    AdjustSlider { label: "Magenta"; value: editorPreview.selectiveMagenta; valueText: root.signedPercent(editorPreview.selectiveMagenta); onValueDragged: function(value) { editorPreview.selectiveMagenta = value } }
                                    AdjustSlider { label: "Yellow"; value: editorPreview.selectiveYellow; valueText: root.signedPercent(editorPreview.selectiveYellow); onValueDragged: function(value) { editorPreview.selectiveYellow = value } }
                                    AdjustSlider { label: "Black"; value: editorPreview.selectiveBlack; valueText: root.signedPercent(editorPreview.selectiveBlack); onValueDragged: function(value) { editorPreview.selectiveBlack = value } }
                                }

                                    CollapsibleGroup {
                                    objectName: "channelMixerGroup"
                                    title: "Channel Mixer"
                                    expanded: root.channelMixerExpanded
                                    onToggled: root.channelMixerExpanded = !root.channelMixerExpanded
                                    AdjustSlider { objectName: "channelMixerRedFromRedSlider"; label: "Red / Red"; from: -200; to: 200; defaultValue: 100; value: editorPreview.channelMixerRedFromRed; valueText: Math.round(editorPreview.channelMixerRedFromRed).toString(); onValueDragged: function(value) { editorPreview.channelMixerRedFromRed = value } }
                                    AdjustSlider { label: "Red / Green"; from: -200; to: 200; value: editorPreview.channelMixerRedFromGreen; valueText: Math.round(editorPreview.channelMixerRedFromGreen).toString(); onValueDragged: function(value) { editorPreview.channelMixerRedFromGreen = value } }
                                    AdjustSlider { label: "Red / Blue"; from: -200; to: 200; value: editorPreview.channelMixerRedFromBlue; valueText: Math.round(editorPreview.channelMixerRedFromBlue).toString(); onValueDragged: function(value) { editorPreview.channelMixerRedFromBlue = value } }
                                    AdjustSlider { label: "Green / Red"; from: -200; to: 200; value: editorPreview.channelMixerGreenFromRed; valueText: Math.round(editorPreview.channelMixerGreenFromRed).toString(); onValueDragged: function(value) { editorPreview.channelMixerGreenFromRed = value } }
                                    AdjustSlider { label: "Green / Green"; from: -200; to: 200; defaultValue: 100; value: editorPreview.channelMixerGreenFromGreen; valueText: Math.round(editorPreview.channelMixerGreenFromGreen).toString(); onValueDragged: function(value) { editorPreview.channelMixerGreenFromGreen = value } }
                                    AdjustSlider { label: "Green / Blue"; from: -200; to: 200; value: editorPreview.channelMixerGreenFromBlue; valueText: Math.round(editorPreview.channelMixerGreenFromBlue).toString(); onValueDragged: function(value) { editorPreview.channelMixerGreenFromBlue = value } }
                                    AdjustSlider { label: "Blue / Red"; from: -200; to: 200; value: editorPreview.channelMixerBlueFromRed; valueText: Math.round(editorPreview.channelMixerBlueFromRed).toString(); onValueDragged: function(value) { editorPreview.channelMixerBlueFromRed = value } }
                                    AdjustSlider { label: "Blue / Green"; from: -200; to: 200; value: editorPreview.channelMixerBlueFromGreen; valueText: Math.round(editorPreview.channelMixerBlueFromGreen).toString(); onValueDragged: function(value) { editorPreview.channelMixerBlueFromGreen = value } }
                                    AdjustSlider { label: "Blue / Blue"; from: -200; to: 200; defaultValue: 100; value: editorPreview.channelMixerBlueFromBlue; valueText: Math.round(editorPreview.channelMixerBlueFromBlue).toString(); onValueDragged: function(value) { editorPreview.channelMixerBlueFromBlue = value } }
                                }

                                    CollapsibleGroup {
                                    objectName: "blackWhiteGroup"
                                    title: "Black & White"
                                    expanded: root.blackWhiteExpanded
                                    onToggled: root.blackWhiteExpanded = !root.blackWhiteExpanded
                                    CheckBox {
                                        text: "Enable"
                                        checked: editorPreview.blackAndWhiteEnabled
                                        onToggled: editorPreview.blackAndWhiteEnabled = checked
                                    }
                                    AdjustSlider { label: "Reds"; value: editorPreview.blackAndWhiteReds; valueText: root.signedPercent(editorPreview.blackAndWhiteReds); onValueDragged: function(value) { editorPreview.blackAndWhiteReds = value } }
                                    AdjustSlider { label: "Oranges"; value: editorPreview.blackAndWhiteOranges; valueText: root.signedPercent(editorPreview.blackAndWhiteOranges); onValueDragged: function(value) { editorPreview.blackAndWhiteOranges = value } }
                                    AdjustSlider { label: "Yellows"; value: editorPreview.blackAndWhiteYellows; valueText: root.signedPercent(editorPreview.blackAndWhiteYellows); onValueDragged: function(value) { editorPreview.blackAndWhiteYellows = value } }
                                    AdjustSlider { label: "Greens"; value: editorPreview.blackAndWhiteGreens; valueText: root.signedPercent(editorPreview.blackAndWhiteGreens); onValueDragged: function(value) { editorPreview.blackAndWhiteGreens = value } }
                                    AdjustSlider { label: "Aquas"; value: editorPreview.blackAndWhiteAquas; valueText: root.signedPercent(editorPreview.blackAndWhiteAquas); onValueDragged: function(value) { editorPreview.blackAndWhiteAquas = value } }
                                    AdjustSlider { label: "Blues"; value: editorPreview.blackAndWhiteBlues; valueText: root.signedPercent(editorPreview.blackAndWhiteBlues); onValueDragged: function(value) { editorPreview.blackAndWhiteBlues = value } }
                                    AdjustSlider { label: "Purples"; value: editorPreview.blackAndWhitePurples; valueText: root.signedPercent(editorPreview.blackAndWhitePurples); onValueDragged: function(value) { editorPreview.blackAndWhitePurples = value } }
                                    AdjustSlider { label: "Magentas"; value: editorPreview.blackAndWhiteMagentas; valueText: root.signedPercent(editorPreview.blackAndWhiteMagentas); onValueDragged: function(value) { editorPreview.blackAndWhiteMagentas = value } }
                                }

                                    CollapsibleGroup {
                                    objectName: "colorReplaceGroup"
                                    title: "Color Replace"
                                    expanded: root.colorReplaceExpanded
                                    onToggled: root.colorReplaceExpanded = !root.colorReplaceExpanded
                                    CheckBox {
                                        text: "Enable"
                                        checked: editorPreview.colorReplacementEnabled
                                        onToggled: editorPreview.colorReplacementEnabled = checked
                                    }
                                    AdjustSlider { label: "Source Hue"; from: 0; to: 1; valueInputMode: "degreesNormalized"; value: editorPreview.colorReplacementSourceHue; valueText: Math.round(editorPreview.colorReplacementSourceHue * 360) + "°"; onValueDragged: function(value) { editorPreview.colorReplacementSourceHue = value } }
                                    AdjustSlider { objectName: "colorReplacementTargetHueSlider"; label: "Target Hue"; from: 0; to: 1; defaultValue: 0.33; valueInputMode: "degreesNormalized"; value: editorPreview.colorReplacementTargetHue; valueText: Math.round(editorPreview.colorReplacementTargetHue * 360) + "°"; onValueDragged: function(value) { editorPreview.colorReplacementTargetHue = value } }
                                    AdjustSlider { objectName: "colorReplacementRangeSlider"; label: "Range"; from: 1; to: 100; defaultValue: 35; value: editorPreview.colorReplacementRange; valueText: Math.round(editorPreview.colorReplacementRange).toString(); onValueDragged: function(value) { editorPreview.colorReplacementRange = value } }
                                    AdjustSlider { label: "Strength"; from: 0; to: 100; value: editorPreview.colorReplacementStrength; valueText: Math.round(editorPreview.colorReplacementStrength).toString(); onValueDragged: function(value) { editorPreview.colorReplacementStrength = value } }
                                }

                                    CollapsibleGroup {
                                    objectName: "skinToneGroup"
                                    title: "Skin Tone"
                                    expanded: root.skinToneExpanded
                                    onToggled: root.skinToneExpanded = !root.skinToneExpanded
                                    CheckBox {
                                        text: "Protect"
                                        checked: editorPreview.skinToneProtectionEnabled
                                        onToggled: editorPreview.skinToneProtectionEnabled = checked
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 28
                                        color: "transparent"
                                        border.color: root.hairlineStrong
                                        border.width: 1
                                        Rectangle {
                                            width: parent.width * 0.82
                                            height: 2
                                            radius: 1
                                            color: root.mint
                                            anchors.centerIn: parent
                                            rotation: editorPreview.skinToneLineAngle - 20
                                        }
                                    }
                                    AdjustSlider { objectName: "skinToneLineHueSlider"; label: "Line Hue"; from: 0; to: 1; defaultValue: 0.08; valueInputMode: "degreesNormalized"; value: editorPreview.skinToneLineHue; valueText: Math.round(editorPreview.skinToneLineAngle) + "°"; onValueDragged: function(value) { editorPreview.skinToneLineHue = value } }
                                    AdjustSlider { label: "Range"; from: 1; to: 100; defaultValue: 30; value: editorPreview.skinToneProtectionRange; valueText: Math.round(editorPreview.skinToneProtectionRange).toString(); onValueDragged: function(value) { editorPreview.skinToneProtectionRange = value } }
                                    AdjustSlider { label: "Strength"; from: 0; to: 100; value: editorPreview.skinToneProtectionStrength; valueText: Math.round(editorPreview.skinToneProtectionStrength).toString(); onValueDragged: function(value) { editorPreview.skinToneProtectionStrength = value } }
                                }

                                    CollapsibleGroup {
                                    objectName: "harmonyGroup"
                                    title: "Harmony"
                                    expanded: root.harmonyExpanded
                                    onToggled: root.harmonyExpanded = !root.harmonyExpanded
                                    CheckBox {
                                        text: "Enable"
                                        checked: editorPreview.colorHarmonyEnabled
                                        onToggled: editorPreview.colorHarmonyEnabled = checked
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Repeater {
                                            model: ["Complementary", "Analogous", "Triadic", "Split Complementary", "Tetradic"]
                                            MiniButton {
                                                required property string modelData
                                                Layout.fillWidth: true
                                                text: modelData === "Split Complementary" ? "Split" : modelData.slice(0, 4)
                                                role: "segmented"
                                                active: editorPreview.colorHarmonyMode === modelData
                                                onClicked: editorPreview.colorHarmonyMode = modelData
                                            }
                                        }
                                    }
                                    AdjustSlider { label: "Base Hue"; from: 0; to: 1; valueInputMode: "degreesNormalized"; value: editorPreview.colorHarmonyBaseHue; valueText: Math.round(editorPreview.colorHarmonyBaseHue * 360) + "°"; onValueDragged: function(value) { editorPreview.colorHarmonyBaseHue = value } }
                                    AdjustSlider { label: "Strength"; from: 0; to: 100; value: editorPreview.colorHarmonyStrength; valueText: Math.round(editorPreview.colorHarmonyStrength).toString(); onValueDragged: function(value) { editorPreview.colorHarmonyStrength = value } }
                                }

                                    CollapsibleGroup {
                                    objectName: "splitToningGroup"
                                    title: "Split Toning"
                                    expanded: root.splitToningExpanded
                                    onToggled: root.splitToningExpanded = !root.splitToningExpanded
                                    RowLayout {
                                        objectName: "splitToningWheelRow"
                                        Layout.fillWidth: true
                                        spacing: 8
                                        ColorWheelControl {
                                            objectName: "splitToningShadowsWheel"
                                            Layout.fillWidth: true
                                            title: "Shadows"
                                            wheelSize: 72
                                            hue: editorPreview.gradingShadowsHue
                                            saturation: editorPreview.gradingShadowsSaturation
                                            luminance: editorPreview.gradingShadowsLuminance
                                            accent: root.mint
                                            onHueEdited: function(value) { root.selectedToneWheel = 0; editorPreview.gradingShadowsHue = value }
                                            onSaturationEdited: function(value) { root.selectedToneWheel = 0; editorPreview.gradingShadowsSaturation = value }
                                        }
                                        ColorWheelControl {
                                            objectName: "splitToningMidtonesWheel"
                                            Layout.fillWidth: true
                                            title: "Midtones"
                                            wheelSize: 72
                                            hue: editorPreview.gradingMidtonesHue
                                            saturation: editorPreview.gradingMidtonesSaturation
                                            luminance: editorPreview.gradingMidtonesLuminance
                                            accent: root.mint
                                            onHueEdited: function(value) { root.selectedToneWheel = 1; editorPreview.gradingMidtonesHue = value }
                                            onSaturationEdited: function(value) { root.selectedToneWheel = 1; editorPreview.gradingMidtonesSaturation = value }
                                        }
                                        ColorWheelControl {
                                            objectName: "splitToningHighlightsWheel"
                                            Layout.fillWidth: true
                                            title: "Highlights"
                                            wheelSize: 72
                                            hue: editorPreview.gradingHighlightsHue
                                            saturation: editorPreview.gradingHighlightsSaturation
                                            luminance: editorPreview.gradingHighlightsLuminance
                                            accent: root.mint
                                            onHueEdited: function(value) { root.selectedToneWheel = 2; editorPreview.gradingHighlightsHue = value }
                                            onSaturationEdited: function(value) { root.selectedToneWheel = 2; editorPreview.gradingHighlightsSaturation = value }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Repeater {
                                            model: ["Shadows", "Midtones", "Highlights"]
                                            MiniButton {
                                                required property int index
                                                required property string modelData
                                                Layout.fillWidth: true
                                                text: modelData
                                                role: "segmented"
                                                active: root.selectedToneWheel === index
                                                onClicked: root.selectedToneWheel = index
                                            }
                                        }
                                    }
                                    AdjustSlider { objectName: "selectedToneWheelHueSlider"; label: root.selectedToneWheelName() + " Hue"; from: 0; to: 1; valueInputMode: "degreesNormalized"; value: root.selectedToneWheelHue(); valueText: Math.round(root.selectedToneWheelHue() * 360) + "°"; onValueDragged: function(value) { root.setSelectedToneWheelHue(value) } }
                                    AdjustSlider { label: root.selectedToneWheelName() + " Sat"; from: 0; to: 100; value: root.selectedToneWheelSaturation(); valueText: Math.round(root.selectedToneWheelSaturation()); onValueDragged: function(value) { root.setSelectedToneWheelSaturation(value) } }
                                    AdjustSlider { label: root.selectedToneWheelName() + " Lum"; value: root.selectedToneWheelLuminance(); valueText: root.signedPercent(root.selectedToneWheelLuminance()); onValueDragged: function(value) { root.setSelectedToneWheelLuminance(value) } }
                                }
                                }

                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.editInspectorTab === "detail"
                                spacing: 12
                                InspectorGroup {
                                    title: "Detail"
                                    resetGroup: "detail"
                                    resetObjectName: "detailPanelResetButton"
                                    AdjustSlider { label: "Sharpening"; value: editorPreview.sharpening; valueText: root.signedPercent(editorPreview.sharpening); onValueDragged: function(value) { editorPreview.sharpening = value } }
                                    AdjustSlider { label: "Masking"; value: editorPreview.sharpenMasking; valueText: root.signedPercent(editorPreview.sharpenMasking); onValueDragged: function(value) { editorPreview.sharpenMasking = value } }
                                    AdjustSlider { label: "Luminance NR"; value: editorPreview.luminanceNr; valueText: root.signedPercent(editorPreview.luminanceNr); onValueDragged: function(value) { editorPreview.luminanceNr = value } }
                                    AdjustSlider { label: "Color NR"; value: editorPreview.colorNr; valueText: root.signedPercent(editorPreview.colorNr); onValueDragged: function(value) { editorPreview.colorNr = value } }
                                }
                                InspectorGroup {
                                    title: "Lens"
                                    resetGroup: "lens"
                                    resetObjectName: "lensPanelResetButton"
                                    AdjustSlider { label: "Distortion"; value: editorPreview.lensDistortion; valueText: root.signedPercent(editorPreview.lensDistortion); onValueDragged: function(value) { editorPreview.lensDistortion = value } }
                                    AdjustSlider { label: "Vignette"; value: editorPreview.lensVignette; valueText: root.signedPercent(editorPreview.lensVignette); onValueDragged: function(value) { editorPreview.lensVignette = value } }
                                    AdjustSlider { label: "Chromatic"; from: 0; to: 100; value: editorPreview.chromaticAberration; valueText: Math.round(editorPreview.chromaticAberration); onValueDragged: function(value) { editorPreview.chromaticAberration = value } }
                                }
                                InspectorGroup {
                                    title: "Crop"
                                    CollapsibleGroup {
                                        objectName: "cropBoxGroup"
                                        title: "Crop Box"
                                        expanded: true
                                        onToggled: expanded = !expanded
                                        AdjustSlider { objectName: "cropZoomSlider"; label: "Zoom"; from: 0.5; to: 4; stepSize: 0.01; defaultValue: 1; value: editorPreview.cropZoom; valueText: editorPreview.cropZoom.toFixed(2) + "x"; onValueDragged: function(value) { editorPreview.cropZoom = value } }
                                        AdjustSlider { objectName: "cropRectXSlider"; label: "Crop X"; from: 0; to: 100; stepSize: 0.1; value: editorPreview.cropRectX * 100; valueText: (editorPreview.cropRectX * 100).toFixed(1) + "%"; onValueDragged: function(value) { editorPreview.updateCropRect(value / 100, editorPreview.cropRectY, editorPreview.cropRectWidth, editorPreview.cropRectHeight) } }
                                        AdjustSlider { objectName: "cropRectYSlider"; label: "Crop Y"; from: 0; to: 100; stepSize: 0.1; value: editorPreview.cropRectY * 100; valueText: (editorPreview.cropRectY * 100).toFixed(1) + "%"; onValueDragged: function(value) { editorPreview.updateCropRect(editorPreview.cropRectX, value / 100, editorPreview.cropRectWidth, editorPreview.cropRectHeight) } }
                                        AdjustSlider { objectName: "cropRectWidthSlider"; label: "Crop W"; from: 5; to: 100; stepSize: 0.1; defaultValue: 100; value: editorPreview.cropRectWidth * 100; valueText: (editorPreview.cropRectWidth * 100).toFixed(1) + "%"; onValueDragged: function(value) { editorPreview.updateCropRect(editorPreview.cropRectX, editorPreview.cropRectY, value / 100, editorPreview.cropRectHeight) } }
                                        AdjustSlider { objectName: "cropRectHeightSlider"; label: "Crop H"; from: 5; to: 100; stepSize: 0.1; defaultValue: 100; value: editorPreview.cropRectHeight * 100; valueText: (editorPreview.cropRectHeight * 100).toFixed(1) + "%"; onValueDragged: function(value) { editorPreview.updateCropRect(editorPreview.cropRectX, editorPreview.cropRectY, editorPreview.cropRectWidth, value / 100) } }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            MiniButton { objectName: "resetCropRectButton"; Layout.fillWidth: true; text: "Reset Crop"; role: "secondary"; onClicked: editorPreview.resetCropRect() }
                                            MiniButton { objectName: "resetTransformButton"; Layout.fillWidth: true; text: "Reset Transform"; role: "secondary"; onClicked: editorPreview.resetTransform() }
                                        }
                                    }
                                    CollapsibleGroup {
                                        objectName: "cropGuideAspectGroup"
                                        title: "Guides & Aspect"
                                        expanded: true
                                        onToggled: expanded = !expanded
                                        RowLayout {
                                            Layout.fillWidth: true
                                            MiniButton { objectName: "cropGuideOffButton"; Layout.fillWidth: true; text: "Off"; role: "segmented"; active: root.cropGuideMode === "Off"; onClicked: root.cropGuideMode = "Off" }
                                            MiniButton { objectName: "cropGuideThirdsButton"; Layout.fillWidth: true; text: "Thirds"; role: "segmented"; active: root.cropGuideMode === "Thirds"; onClicked: root.cropGuideMode = "Thirds" }
                                            MiniButton { objectName: "cropGuideGoldenButton"; Layout.fillWidth: true; text: "Golden"; role: "segmented"; active: root.cropGuideMode === "Golden Ratio"; onClicked: root.cropGuideMode = "Golden Ratio" }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            MiniButton { objectName: "cropGuideCenterButton"; Layout.fillWidth: true; text: "Center"; role: "segmented"; active: root.cropGuideMode === "Center"; onClicked: root.cropGuideMode = "Center" }
                                            MiniButton { objectName: "cropGuideDiagonalButton"; Layout.fillWidth: true; text: "Diag"; role: "segmented"; active: root.cropGuideMode === "Diagonal"; onClicked: root.cropGuideMode = "Diagonal" }
                                            MiniButton { objectName: "cropGuideSpiralButton"; Layout.fillWidth: true; text: "Spiral"; role: "segmented"; active: root.cropGuideMode === "Golden Spiral"; onClicked: root.cropGuideMode = "Golden Spiral" }
                                        }
                                        Label { text: "Aspect"; color: root.dimText; font.family: theme.fontUi; font.pixelSize: 10; font.letterSpacing: 0 }
                                        ToneComboBox {
                                            objectName: "cropAspectPresetComboBox"
                                            Layout.fillWidth: true
                                            model: editorPreview.cropAspectPresets()
                                            currentIndex: model.indexOf(editorPreview.cropPreset)
                                            onActivated: editorPreview.cropPreset = currentText
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            ToneTextField {
                                                id: cropAspectPresetInput
                                                objectName: "cropAspectPresetInput"
                                                Layout.fillWidth: true
                                                placeholderText: "2.39:1"
                                                text: ""
                                                onAccepted: {
                                                    if (root.addCropAspectPreset(text))
                                                        text = ""
                                                }
                                            }
                                            MiniButton {
                                                objectName: "addCropAspectPresetButton"
                                                text: "Add"
                                                role: "secondary"
                                                enabled: cropAspectPresetInput.text.trim().length > 0
                                                onClicked: {
                                                    if (root.addCropAspectPreset(cropAspectPresetInput.text))
                                                        cropAspectPresetInput.text = ""
                                                }
                                            }
                                            MiniButton {
                                                objectName: "removeCropAspectPresetButton"
                                                text: "Remove"
                                                role: "destructive"
                                                enabled: root.cropPresetIsCustom(editorPreview.cropPreset)
                                                onClicked: root.removeCropAspectPreset(editorPreview.cropPreset)
                                            }
                                        }
                                    }
                                    CollapsibleGroup {
                                        objectName: "cropTransformGroup"
                                        title: "Transform & Upright"
                                        expanded: root.cropTransformExpanded
                                        onToggled: root.cropTransformExpanded = !root.cropTransformExpanded
                                        RowLayout {
                                            Layout.fillWidth: true
                                            MiniButton { Layout.fillWidth: true; text: "Off"; role: "segmented"; active: editorPreview.uprightMode === "Off"; onClicked: editorPreview.uprightMode = "Off" }
                                            MiniButton { Layout.fillWidth: true; text: "Guided"; role: "segmented"; active: editorPreview.uprightMode === "Guided"; onClicked: editorPreview.uprightMode = "Guided" }
                                        }
                                        AdjustSlider { label: "V Guide"; from: -45; to: 45; value: editorPreview.uprightVerticalGuideAngle; valueText: Math.round(editorPreview.uprightVerticalGuideAngle) + " deg"; onValueDragged: function(value) { editorPreview.uprightVerticalGuideAngle = value } }
                                        AdjustSlider { label: "H Guide"; from: -45; to: 45; value: editorPreview.uprightHorizontalGuideAngle; valueText: Math.round(editorPreview.uprightHorizontalGuideAngle) + " deg"; onValueDragged: function(value) { editorPreview.uprightHorizontalGuideAngle = value } }
                                        MiniButton { Layout.fillWidth: true; text: "Straighten Horizon"; role: "secondary"; enabled: editorPreview.hasAsset && Math.abs(editorPreview.uprightHorizontalGuideAngle) > 0.05; onClicked: editorPreview.straightenFromHorizonAngle(editorPreview.uprightHorizontalGuideAngle) }
                                        MiniButton { Layout.fillWidth: true; text: "Apply Upright"; role: "secondary"; onClicked: editorPreview.applyGuidedUpright() }
                                        MiniButton { Layout.fillWidth: true; text: "Crop Fill"; role: "segmented"; active: editorPreview.contentAwareCropFill; onClicked: editorPreview.contentAwareCropFill = !editorPreview.contentAwareCropFill }
                                        AdjustSlider { label: "Rotate"; from: -45; to: 45; stepSize: 0.1; value: editorPreview.transformRotation; valueText: editorPreview.transformRotation.toFixed(1) + " deg"; onValueDragged: function(value) { editorPreview.transformRotation = value } }
                                        AdjustSlider { objectName: "transformScaleSlider"; label: "Scale"; from: 0.25; to: 4; stepSize: 0.01; defaultValue: 1; value: editorPreview.transformScale; valueText: editorPreview.transformScale.toFixed(2) + "x"; onValueDragged: function(value) { editorPreview.transformScale = value } }
                                        AdjustSlider { label: "Vertical"; from: -100; to: 100; value: editorPreview.perspectiveVertical; valueText: root.signedPercent(editorPreview.perspectiveVertical); onValueDragged: function(value) { editorPreview.perspectiveVertical = value } }
                                        AdjustSlider { label: "Horizontal"; from: -100; to: 100; value: editorPreview.perspectiveHorizontal; valueText: root.signedPercent(editorPreview.perspectiveHorizontal); onValueDragged: function(value) { editorPreview.perspectiveHorizontal = value } }
                                        AdjustSlider { label: "Skew X"; from: -100; to: 100; value: editorPreview.transformSkewX; valueText: root.signedPercent(editorPreview.transformSkewX); onValueDragged: function(value) { editorPreview.transformSkewX = value } }
                                        AdjustSlider { label: "Skew Y"; from: -100; to: 100; value: editorPreview.transformSkewY; valueText: root.signedPercent(editorPreview.transformSkewY); onValueDragged: function(value) { editorPreview.transformSkewY = value } }
                                    }
                                    CollapsibleGroup {
                                        objectName: "cropLiquifyGroup"
                                        title: "Liquify"
                                        expanded: root.cropLiquifyExpanded
                                        onToggled: root.cropLiquifyExpanded = !root.cropLiquifyExpanded
                                        AdjustSlider { label: "Strength"; from: -100; to: 100; value: editorPreview.liquifyStrength; valueText: root.signedPercent(editorPreview.liquifyStrength); onValueDragged: function(value) { editorPreview.liquifyStrength = value } }
                                        AdjustSlider { objectName: "liquifyRadiusSlider"; label: "Radius"; from: 0.02; to: 1; stepSize: 0.01; defaultValue: 0.25; valueInputMode: "percentNormalized"; value: editorPreview.liquifyRadius; valueText: Math.round(editorPreview.liquifyRadius * 100) + "%"; onValueDragged: function(value) { editorPreview.liquifyRadius = value } }
                                        AdjustSlider { label: "Center X"; from: 0; to: 1; stepSize: 0.01; defaultValue: 0.5; valueInputMode: "percentNormalized"; value: editorPreview.liquifyCenterX; valueText: Math.round(editorPreview.liquifyCenterX * 100) + "%"; onValueDragged: function(value) { editorPreview.liquifyCenterX = value } }
                                        AdjustSlider { label: "Center Y"; from: 0; to: 1; stepSize: 0.01; defaultValue: 0.5; valueInputMode: "percentNormalized"; value: editorPreview.liquifyCenterY; valueText: Math.round(editorPreview.liquifyCenterY * 100) + "%"; onValueDragged: function(value) { editorPreview.liquifyCenterY = value } }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.editInspectorTab === "scopes"
                                spacing: 12

                                InspectorGroup {
                                    title: "Scope Controls"
                                    Label {
                                        objectName: "scopeAsyncStatusLabel"
                                        Layout.fillWidth: true
                                        text: editorPreview.histogramUpdateActive
                                              ? "Scopes updating in background"
                                              : "Scopes current"
                                        color: editorPreview.histogramUpdateActive ? theme.accentWarm400 : theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        MiniButton { Layout.fillWidth: true; text: "RGB"; role: "segmented"; active: root.scopeShowRgb; onClicked: root.scopeShowRgb = !root.scopeShowRgb }
                                        MiniButton { Layout.fillWidth: true; text: "Luma"; role: "segmented"; active: root.scopeShowLuma; onClicked: root.scopeShowLuma = !root.scopeShowLuma }
                                        MiniButton { Layout.fillWidth: true; text: root.scopeLogHistogram ? "Log" : "Linear"; role: "segmented"; active: root.scopeLogHistogram; onClicked: root.scopeLogHistogram = !root.scopeLogHistogram }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        MiniButton { Layout.fillWidth: true; text: "Density"; role: "segmented"; active: root.scopeShowDensity; onClicked: root.scopeShowDensity = !root.scopeShowDensity }
                                        MiniButton { Layout.fillWidth: true; text: "Trace"; role: "segmented"; active: root.scopeShowTrace; onClicked: root.scopeShowTrace = !root.scopeShowTrace }
                                        MiniButton { Layout.fillWidth: true; text: "Grid"; role: "segmented"; active: root.scopeShowGrid; onClicked: root.scopeShowGrid = !root.scopeShowGrid }
                                    }
                                    MiniButton {
                                        Layout.fillWidth: true
                                        text: root.largeScopesMode ? "Show Image" : "Large Scopes"
                                        iconName: "scope"
                                        role: "secondary"
                                        active: root.largeScopesMode
                                        onClicked: root.largeScopesMode = !root.largeScopesMode
                                    }
                                }

                                ScopeGrid {
                                    Layout.fillWidth: true
                                    large: false
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.editInspectorTab === "info"
                                spacing: 10
                                InspectorGroup {
                                    title: "Asset"
                                    Repeater {
                                        model: [
                                            ["File", root.selectedName()],
                                            ["Camera", editorPreview.cameraLabel.length > 0 ? editorPreview.cameraLabel : "DJI \"ZH20T\""],
                                            ["Lens", editorPreview.lensModel.length > 0 ? editorPreview.lensModel : "6.8-119.9 mm f/4.0"],
                                            ["Captured", editorPreview.captureDate.length > 0 ? editorPreview.captureDate : "Oct 1, 2025 9:57 PM"],
                                            ["Color Space", editorPreview.outputColorSpace]
                                        ]
                                        RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Label { Layout.preferredWidth: 92; text: modelData[0]; color: root.dimText; font.pixelSize: 11 }
                                            Label { Layout.fillWidth: true; text: modelData[1]; color: theme.textMain; font.pixelSize: 11; elide: Text.ElideRight }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        MiniButton { Layout.preferredWidth: 74; text: "Reset"; role: "secondary"; onClicked: editorPreview.resetAdjustments() }
                        MiniButton { Layout.fillWidth: true; text: "Save Preset"; role: "secondary"; onClicked: editorPreview.savePreset("Quick Preset", true) }
                        MiniButton { Layout.fillWidth: true; text: "Export"; role: "primary"; active: true; onClicked: root.assetActivated("export") }
                    }
                }
            }
        }
    }

    Component {
        id: exportPage

        RowLayout {
            spacing: 0

            Panel {
                objectName: "exportPresetsPanel"
                visible: root.exportPresetsPanelVisible
                Layout.preferredWidth: root.exportPresetsPanelVisible ? root.exportPresetsPanelWidth : 0
                Layout.fillHeight: true
                title: "Export Presets"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    MiniButton { Layout.fillWidth: true; text: "+ New Preset"; role: "secondary" }
                    Repeater {
                        model: root.exportPresets
                        SidebarRow {
                            required property int index
                            required property var modelData
                            label: modelData.name
                            value: modelData.format
                            active: index === root.selectedPresetIndex
                            onClicked: root.selectedPresetIndex = index
                        }
                    }
                    MiniButton { Layout.fillWidth: true; text: "Manage Presets"; role: "secondary" }
                    RowRule {}
                    SectionTitle { label: "Destinations" }
                    Repeater {
                        model: [["Local Drive", "/Volumes/Exports"], ["VIREL Cloud", "Connected"], ["Dropbox", "brad@example.com"], ["Google Drive", "brad@example.com"], ["OneDrive", "brad@example.com"]]
                        SidebarRow { required property int index; required property var modelData; label: modelData[0]; value: modelData[1]; active: index < 2; onClicked: root.exportDestination = modelData[1] }
                    }
                    Item { Layout.fillHeight: true }
                    SectionTitle { label: "Export Drive" }
                    ProgressBar { Layout.fillWidth: true; value: 0.30 }
                    Label { text: "2.4 TB / 8 TB                                      30%"; color: root.dimText; font.pixelSize: 10 }
                }
            }

            Rectangle { Layout.preferredWidth: 0; Layout.fillHeight: true; color: "transparent" }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 20
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    SectionTitle { Layout.fillWidth: true; label: "Selected Assets  " + Math.max(2, catalogAssets.selectedAssetCount) }
                    MiniButton { text: "Presets"; role: "segmented"; active: root.exportPresetsPanelVisible; onClicked: root.exportPresetsPanelVisible = !root.exportPresetsPanelVisible }
                    MiniButton { text: "Settings"; role: "segmented"; active: root.exportSettingsPanelVisible; onClicked: root.exportSettingsPanelVisible = !root.exportSettingsPanelVisible }
                    MiniButton { text: root.compactMode ? "Pro" : "Compact"; role: "segmented"; active: root.compactMode; onClicked: root.toggleInterfaceMode() }
                    MiniButton { text: "+ Add More"; role: "secondary" }
                    MiniButton { square: true; iconName: "delete"; role: "destructive" }
                }

                EmptyStateCard {
                    objectName: "exportEmptyState"
                    visible: root.exportEmptyStateTitle().length > 0
                    iconName: "export"
                    title: root.exportEmptyStateTitle()
                    message: root.exportEmptyStateMessage()
                }

                RowLayout {
                    visible: root.exportEmptyStateTitle().length === 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    spacing: 12
                    Repeater {
                        model: Math.min(2, Math.max(1, root.assetCount()))
                        ThumbnailCard {
                            required property int index
                            Layout.preferredWidth: 282
                            Layout.fillHeight: true
                            assetIndex: index
                            selected: true
                        }
                    }
                }

                RowRule {}
                RowLayout {
                    visible: root.exportEmptyStateTitle().length === 0
                    Layout.fillWidth: true
                    SectionTitle { Layout.fillWidth: true; label: "Export Queue  -  " + Math.max(2, jobQueue.jobCount) + " jobs" }
                    MiniButton { text: "Clear Queue"; iconName: "delete"; role: "destructive"; onClicked: jobQueue.clearFinishedJobs() }
                    MiniButton {
                        text: "Start Export"
                        role: "primary"
                        active: true
                        onClicked: {
                            if (catalogAssets.selectedIndex >= 0)
                                root.queueExport(catalogAssets.selectedIndex)
                            else
                                root.queueExport(0)
                        }
                    }
                }

                Rectangle {
                    visible: root.exportEmptyStateTitle().length === 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 196
                    color: root.panelColor
                    border.width: theme.chromeBorderWidth
                    border.color: root.hairline
                    radius: 4

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 0

                        Repeater {
                            model: Math.min(2, Math.max(1, root.assetCount()))
                            Rectangle {
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredHeight: 62
                                color: index % 2 === 0 ? "#0B1012" : "#0E1416"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12
                                    Label { text: String(index + 1); color: theme.textMain; font.family: theme.fontMono; font.pixelSize: 10 }
                                    Image { source: root.assetThumb(index); Layout.preferredWidth: 44; Layout.preferredHeight: 34; fillMode: Image.PreserveAspectCrop }
                                    Label { Layout.fillWidth: true; text: root.assetName(index); color: theme.textStrong; font.pixelSize: 11; elide: Text.ElideRight }
                                    Label { text: root.exportPresets[root.selectedPresetIndex].name; color: theme.textMain; font.family: theme.fontMono; font.pixelSize: 10 }
                                    Label { text: root.exportPresets[root.selectedPresetIndex].format; color: theme.textMain; font.family: theme.fontMono; font.pixelSize: 10 }
                                    Label { text: "2048 px"; color: theme.textMain; font.family: theme.fontMono; font.pixelSize: 10 }
                                    Label { text: "Ready"; color: root.mint; font.family: theme.fontMono; font.pixelSize: 10 }
                                    MiniButton { square: true; iconName: "play"; role: "icon"; onClicked: root.queueExport(index) }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 14
                            color: "#081113"
                            border.width: theme.chromeBorderWidth
                            border.color: theme.alpha(root.mint, 0.28)
                            radius: 4
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                AppIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "plus"; width: 28; height: 28; color: root.mint }
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Drag more assets here"; color: root.mint; font.family: theme.fontUi; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0 }
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Add files or folders to the export queue"; color: root.dimText; font.pixelSize: 10 }
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 0; Layout.fillHeight: true; color: "transparent" }

            Panel {
                objectName: "exportSettingsPanel"
                visible: root.exportSettingsPanelVisible
                Layout.preferredWidth: root.exportSettingsPanelVisible ? root.exportSettingsPanelWidth : 0
                Layout.fillHeight: true
                title: "Output Settings"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Repeater {
                        model: [
                            ["Output Preset", root.exportPresets[root.selectedPresetIndex].name],
                            ["Color Space", "sRGB IEC61966-2.1"],
                            ["Resize", "Long Edge            2048 px"],
                            ["Sharpening", "Standard            Amount 40"],
                            ["Watermark", "No Watermark"],
                            ["File Naming", "Custom Name        " + root.exportNameSuffix],
                            ["Destination", "Local Drive        " + root.exportDestination]
                        ]
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 66
                            color: "#0C1113"
                            border.width: theme.chromeBorderWidth
                            border.color: root.hairline
                            radius: 4
                            Column {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6
                                Label { text: modelData[0]; color: root.dimText; font.family: theme.fontUi; font.pixelSize: 9; font.letterSpacing: 0 }
                                Label { text: modelData[1]; color: theme.textStrong; font.family: theme.fontMono; font.pixelSize: 12; elide: Text.ElideRight; width: parent.width }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 134
                        color: "#0C1113"
                        border.width: theme.chromeBorderWidth
                        border.color: root.hairline
                        radius: 4
                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8
                            SectionTitle { label: "EXPORT SUMMARY" }
                            Label { text: "2 Assets"; color: root.dimText; font.pixelSize: 11 }
                            Label { text: "2 Total Outputs"; color: root.dimText; font.pixelSize: 11 }
                            Label { text: root.exportPresets[root.selectedPresetIndex].format + "  -  90% Quality"; color: root.dimText; font.pixelSize: 11 }
                            Label { text: "Est. Total Size  ~2.3 MB"; color: root.dimText; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }
}
