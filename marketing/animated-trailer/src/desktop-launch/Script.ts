export type LaunchShot = {
  id: string;
  start: number;
  end: number;
  chapter: string;
  title: string;
  copy: string;
  supporting: string;
  action: string;
  continuity: string;
  capture: string;
};

// The production order is fixed. Revisions change a shot without silently
// deleting capabilities, changing the score, or replacing the visual language.
export const launchShots: LaunchShot[] = [
  {
    id: "introduce",
    start: 0,
    end: 8,
    chapter: "INTRO",
    title: "Meet Looksy",
    copy: "Looksy.",
    supporting: "The desktop photo + video studio.",
    action:
      "Textured additive light resolves into the real wordmark. The actual RAW editor arrives within the first four seconds. No wedding cold open.",
    continuity: "The native window stays in place as the first chapter begins.",
    capture: "raw-native.mp4",
  },
  {
    id: "raw",
    start: 8,
    end: 17,
    chapter: "01 / PHOTO EDITOR",
    title: "Start with the original",
    copy: "Start with the RAW.",
    supporting: "Camera RAW. Native resolution.",
    action:
      "Open IMG_4969.CR2 in the real desktop editor. Establish the whole window, then connect the RAW exposure control to the photograph. Keep the file identity visible.",
    continuity: "One camera move follows the slider into its image result.",
    capture: "raw-native.mp4",
  },
  {
    id: "engine",
    start: 17,
    end: 26,
    chapter: "01 / PHOTO EDITOR",
    title: "The color engine",
    copy: "A custom color engine.",
    supporting: "Built from the ground up.",
    action:
      "Use native exposure and white balance. The same photograph changes visibly. RGB light bends around the native window; it never substitutes for an edit.",
    continuity: "Hold the developed image before moving into tone.",
    capture: "raw-native.mp4",
  },
  {
    id: "tone",
    start: 26,
    end: 36,
    chapter: "01 / PHOTO EDITOR",
    title: "Tone and color",
    copy: "Shape the light.\nRefine the color.",
    supporting: "Exposure · tone curves · individual color ranges",
    action:
      "Record a restrained tone curve, then a selective blue adjustment in native Color Mix. Show the chosen channel, the control and the affected pixels together.",
    continuity:
      "Keep the actual image crop registered across before and after.",
    capture: "CAPTURED: desktop-action/tone-curve.mp4 + color-edit.mp4; final editorial timing review required",
  },
  {
    id: "detail",
    start: 36,
    end: 45,
    chapter: "01 / PHOTO EDITOR",
    title: "Pixels and detail",
    copy: "Every pixel.\nOn your terms.",
    supporting: "Sharpening · noise reduction · full-resolution detail",
    action:
      "Use the real 100% viewer and Detail panel. Show a clean native pixel crop, then pull back to the entire photograph. No invented pixel grid or simulated slider.",
    continuity: "The finished photo becomes a library thumbnail.",
    capture: "NEEDED: native-detail.mov",
  },
  {
    id: "photo-delivery",
    start: 45,
    end: 52,
    chapter: "01 / PHOTO EDITOR",
    title: "Photographic delivery",
    copy: "Finish for the way you work.",
    supporting: "Color-managed preview and export.",
    action:
      "Show the native color pipeline and export choices, followed by finished travel and wedding photographs. Wedding imagery appears as a workflow example after the editor is established.",
    continuity: "Finished pictures assemble into the library grid.",
    capture: "NEEDED: native-photo-export.mov",
  },
  {
    id: "library",
    start: 52,
    end: 61,
    chapter: "02 / LIBRARY",
    title: "The whole shoot",
    copy: "Bring the whole shoot.",
    supporting: "Batch import. Originals organized.",
    action:
      "Show the actual import review and the completed native library with the copied camera files and video originals. Move through one real grid, not floating duplicate PNGs.",
    continuity:
      "The selected original expands into Cull without changing identity.",
    capture: "library.png",
  },
  {
    id: "cull",
    start: 61,
    end: 69,
    chapter: "02 / LIBRARY",
    title: "Keep the keepers",
    copy: "Find your selects.",
    supporting: "Rate · pick · compare · cull",
    action:
      "Native culling: mark a real demo copy, advance to the next photo, compare two candidates and inspect original detail.",
    continuity: "The selected set stays selected for batch editing.",
    capture: "cull-native.mp4",
  },
  {
    id: "batch",
    start: 69,
    end: 76,
    chapter: "02 / LIBRARY",
    title: "A consistent set",
    copy: "One finish. Across the set.",
    supporting: "Batch adjustments with room to refine each image.",
    action:
      "Use native batch edit on licensed wedding JPEGs or actual camera RAWs. Show the real selected count, synchronized changes and a coherent finished pair. Never label JPEGs RAW.",
    continuity: "Return to the same library before search.",
    capture: "NEEDED: native-batch.mov",
  },
  {
    id: "search",
    start: 76,
    end: 84,
    chapter: "02 / LIBRARY",
    title: "Search what is in the picture",
    copy: "Find the image you remember.",
    supporting: "Local visual search.",
    action:
      "Run an actual query such as boats on a lake. Show the real model mode and ranked result. Similarity ranking is not described as verified semantic detection.",
    continuity: "Open the selected lake image into the canvas chapter.",
    capture: "NEEDED: native-search.mov; model setup currently incomplete",
  },
  {
    id: "canvas",
    start: 84,
    end: 93,
    chapter: "03 / CANVAS",
    title: "More than a photograph",
    copy: "Make something from it.",
    supporting: "A canvas for photos, graphics and ideas.",
    action:
      "Show a finished editorial poster, a wide thumbnail and the real La Esquina design, then reveal the native layer stack. Avoid plain centered text over a random photo.",
    continuity:
      "Select the source photo layer inside the finished composition.",
    capture: "canvas.png",
  },
  {
    id: "cutout",
    start: 93,
    end: 102,
    chapter: "03 / CANVAS",
    title: "Cutout and masks",
    copy: "Select. Mask. Cut out.",
    supporting: "Precise edges. Editable layers.",
    action:
      "Use native subject cutout on a still photograph. Refine the actual edge and demonstrate the layer mask in the real layer stack. No walking-video title occlusion.",
    continuity: "The same mask remains selected as the brush is introduced.",
    capture: "PARTIAL: native subject selection and masked exposure captured; transparent cutout/refine-edges demonstration still needed",
  },
  {
    id: "brush",
    start: 102,
    end: 111,
    chapter: "03 / CANVAS",
    title: "Purposeful brushwork",
    copy: "Paint with purpose.",
    supporting: "Brushes, masks and pixel-level control.",
    action:
      "Use an actual textured brush to create a coherent print treatment on a separate layer, then refine a deliberate masked area. Show brush size, stroke and layer thumbnail. No decorative squiggle.",
    continuity:
      "Move from the mark to a small distracting object in the same scene.",
    capture: "NEEDED: native-brush.mov",
  },
  {
    id: "inpaint",
    start: 111,
    end: 119,
    chapter: "03 / CANVAS",
    title: "Retouch the distraction",
    copy: "Remove the distraction.",
    supporting: "Retouching and inpainting.",
    action:
      "Establish a clearly visible unwanted object, paint the native retouch mask, run the app tool and reveal the genuine result in context. Do not use skin blemishes or an imperceptible speck.",
    continuity: "Return to the designed composition at the same scale.",
    capture: "NEEDED: native-inpaint.mov",
  },
  {
    id: "type",
    start: 119,
    end: 128,
    chapter: "03 / CANVAS",
    title: "Editable design",
    copy: "Type. Texture. Layers.",
    supporting: "Posters · thumbnails · graphics",
    action:
      "Select real editable type, adjust tracking and layout, toggle a graphic layer and show the finished design. The native bounding box and panel explain the tool; external motion introduces the result.",
    continuity: "The poster edge becomes the first carousel canvas.",
    capture: "PARTIAL: native editable poster text and layer stack captured; finished designed native poster/thumbnail recreation still needed",
  },
  {
    id: "carousel",
    start: 128,
    end: 138,
    chapter: "04 / CAROUSEL",
    title: "One photograph across canvases",
    copy: "Let the image keep going.",
    supporting: "One continuous photograph. Multiple canvases.",
    action:
      "Use a strong wide mountain panorama spanning three native 4:5 boards. Show the actual connected canvas overview and shared image/layers. Distinct designed title, image and closing boards, not three repeated crops.",
    continuity:
      "Track along the exact shared horizon and preserve its coordinates.",
    capture: "NEEDED: native-panorama.mov",
  },
  {
    id: "carousel-crop",
    start: 138,
    end: 146,
    chapter: "04 / CAROUSEL",
    title: "Preserve the seam",
    copy: "Change the crop. Keep the seam.",
    supporting: "Edit the spanning image together.",
    action:
      "Move the actual spanning source once. All panels change from the same source coordinates. Overlay no invented seam controls; use native boards and real final exports.",
    continuity:
      "The native board spacing expands into a finished swipe preview.",
    capture: "NEEDED: native-panorama-crop.mov",
  },
  {
    id: "carousel-delivery",
    start: 146,
    end: 154,
    chapter: "04 / CAROUSEL",
    title: "Made for the swipe",
    copy: "A sequence worth swiping.",
    supporting: "Design together. Export each canvas.",
    action:
      "Show the full three-board final design, swipe across the seam, then native export of the separate canvases. Preserve readability; no simultaneous bouncing cards.",
    continuity:
      "The final video canvas continues into the opening of the finished film.",
    capture: "NEEDED: native-carousel-export.mov",
  },
  {
    id: "film-result",
    start: 154,
    end: 164,
    chapter: "05 / VIDEO TIMELINE",
    title: "The film first",
    copy: "Make the whole film.",
    supporting: "And the cut made for your feed.",
    action:
      "A ten-second, musically edited Nepal film: arrival, ridge, walking, city/night. Clean editorial title on a full frame. Absolutely no text-behind-walking-person effect.",
    continuity:
      "The exact final film frame shrinks into native Program above its editable timeline.",
    capture: "NEEDED: native-film-result.mp4",
  },
  {
    id: "timeline",
    start: 164,
    end: 173,
    chapter: "05 / VIDEO TIMELINE",
    title: "Build the production",
    copy: "A real production timeline.",
    supporting: "Video · titles · audio · adjustment layers",
    action:
      "Show the actual multi-track native timeline with purposeful names, real footage and an independent sound bed. Trim and ripple one clip, then play the real changed boundary.",
    continuity:
      "Keep the selected clip and the playhead visible while entering motion.",
    capture: "PARTIAL: timeline-build.mov captured a real clip insertion; polished multi-track film and trim still needed",
  },
  {
    id: "animation",
    start: 173,
    end: 182,
    chapter: "05 / VIDEO TIMELINE",
    title: "Motion and effects",
    copy: "Move it. Shape it. Make it yours.",
    supporting: "Keyframes · easing · compositing · effects",
    action:
      "Show native keyframes and Bézier handles controlling a clean title move. Apply one restrained visible image effect and toggle before/after on the same real clip. No effect label without a changed image.",
    continuity:
      "The native effect output stays on screen for color interpretation.",
    capture: "NEEDED: native-animation-effects.mov",
  },
  {
    id: "video-color",
    start: 182,
    end: 191,
    chapter: "05 / VIDEO TIMELINE",
    title: "Video color and formats",
    copy: "Color from capture to delivery.",
    supporting: "Log input · Rec.709 delivery · 4K workflows",
    action:
      "Show actual camera input transform, grading and sequence dimensions. Keep transfer curves/color spaces separate from the ProRes codec choice. Show ProRes in the export codec UI only if available for this build.",
    continuity:
      "A tracked camera move opens the 3D asset inside the same production context.",
    capture: "NEEDED: native-video-color-export.mov",
  },
  {
    id: "gaussian",
    start: 191,
    end: 198,
    chapter: "05 / VIDEO TIMELINE",
    title: "Gaussian scenes",
    copy: "A new perspective.",
    supporting: "Gaussian splat scenes.",
    action:
      "Use the existing genuine native splat renderer and its photographed scene. A short camera orbit with parallax, then return to native production. Do not imply automatic creation from arbitrary source footage.",
    continuity: "Pull out of the scene into the finished production view.",
    capture: "studio/gaussian-camera.mp4; native Scene UI still needed",
  },
  {
    id: "video-delivery",
    start: 198,
    end: 205,
    chapter: "05 / VIDEO TIMELINE",
    title: "Delivery",
    copy: "Full films. Social cuts.",
    supporting: "Choose the frame. Keep the resolution.",
    action:
      "Native landscape production and separately reframed social sequence. Show real export settings and final moving outputs, not social-only messaging.",
    continuity:
      "The finished production remains visible as optional agent assistance appears.",
    capture: "NEEDED: native-video-delivery.mov",
  },
  {
    id: "agent",
    start: 205,
    end: 214,
    chapter: "OPTIONAL / LOCAL AGENT",
    title: "Work alongside an agent",
    copy: "More time for the creative work.",
    supporting: "Optional assistance. Running on your machine.",
    action:
      "Introduce the agent after the manual creative tools. Show the genuine local model connection and a bounded proposal to organize or batch-adjust copied assets. No data-center diagram or fabricated chat.",
    continuity: "The plan enters a real editable change list.",
    capture: "NEEDED: native-local-agent.mov",
  },
  {
    id: "control",
    start: 214,
    end: 224,
    chapter: "OPTIONAL / LOCAL AGENT",
    title: "Keep the controls",
    copy: "Every edit stays yours.",
    supporting: "Editable layers. Reviewable history. Your originals.",
    action:
      "Show actual change review, retained layers and Undo/history. Differentiate the optional LLM assistant from retouching tools; do not claim the whole app contains no AI or inpainting is non-generative.",
    continuity:
      "Real finished photographs, designs, canvases and moving films resolve together.",
    capture: "NEEDED: native-agent-history.mov",
  },
  {
    id: "close",
    start: 224,
    end: 232,
    chapter: "LOOKSY",
    title: "The whole studio",
    copy: "For the work.\nAnd the joy of making it.",
    supporting: "Looksy. Photo + video. On your desktop.",
    action:
      "Three finished outputs and the actual desktop app settle into the wordmark over textured additive light. Final CTA uses only an existing verified product URL, otherwise Looksy wordmark alone.",
    continuity:
      "Natural musical ending; no looping, stretched hold or unrelated extra feature card.",
    capture: "existing native exports + actual app captures",
  },
];

export const launchChapters = [
  { name: "Photo editor", start: 8, end: 52 },
  { name: "Library", start: 52, end: 84 },
  { name: "Canvas", start: 84, end: 128 },
  { name: "Carousel", start: 128, end: 154 },
  { name: "Video timeline", start: 154, end: 205 },
  { name: "Optional local agent", start: 205, end: 224 },
];
