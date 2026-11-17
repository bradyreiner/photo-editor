# Scene camera, color and lens controls

The first native UX pass makes Camera, Lens, Color and Film directly accessible
in the inspector. Sensor calibration and optical diagnostics stay available,
with the routine focus controls above them. Existing scenes are not rewritten.

## Focus

Camera now exposes centre autofocus, focus lock and Pick subject. Option-click
also picks a visible surface. The viewport shows the centre AF target, and the
inspector distinguishes a locked result from a fallback. Manual control combines
an exact distance field, a logarithmic distance slider and 2% Nearer/Farther
steps. Centre AF is a depth query, not person recognition or subject tracking.

## Color and film

Color groups exposure, relative temperature/warmth, green-magenta tint, vibrance,
saturation, faded blacks, contrast and highlight compression. Reset color leaves
lens and film effects intact. Warmth and tint operate relative to captured RGB;
they do not recover an original illuminant or raw white-balance Kelvin.

Six editable film starting points combine grain, halation and color: clean,
soft daylight negative, tungsten night/red glow, vivid slide, faded warm print
and monochrome. These are generic creative recipes, not commercial stock matches.
Choosing a film starting point preserves pose, focus, physical camera and lens
character. Choosing a lens starting point preserves film and color.

The existing film-plane v2 full-RGB light return is retained exactly. The new
opt-in v3 highlight-halation mode applies an exposed linear-luminance cutoff
before RGB diffusion. Its cutoff, return amount, physical diffusion size and RGB
return are editable. The threshold has an effect only in the highlight mode;
v2 continues to transport the complete recorded light. The tungsten starting
point deliberately uses a lower cutoff and stronger red return for visible glow.

## Lens character

Five starting points cover clean spherical, anamorphic character, vintage soft,
diffusion and starburst highlights. An equal-area elliptical entrance pupil
controls vertical/horizontal bokeh aspect from 0.5 to 3. It preserves area and
chief-ray framing; a 2:1 pupil gives vertical oval bokeh without stretching the
captured photograph. Horizontal streaks, bloom, their spatial spread and highlight
cutoff are independently editable. More detailed barrel/aberration and post
controls remain available.

This is a pupil and image-space lens-character model. It does not implement
cylindrical anamorphic projection, measured coating ghosts, optical squeeze,
focus breathing, diffraction or complete compound-glass transport. The displayed
physical pupil diameter is an equivalent-area diameter when the pupil is oval.

Cooke's [anamorphic introduction](https://cookeoptics.com/news-and-events/an-introduction-to-anamorphic-lenses/)
and [1.8x full-frame lens specification](https://cookeoptics.com/lens/anamorphic-i-ff/)
describe the real combination of optical squeeze, oval bokeh and flare. Those
properties motivate separate controls here; this implementation is not a Cooke
profile. Tiffen's [diffusion guide](https://tiffen.com/pages/diffusion-guide)
distinguishes highlight spread and contrast/softening characteristics; the bloom
starting point here is a generic approximation, not a measured filter strength.

## Persistence and verification

The new color model is tone v2, shaped pupils are thin-lens v6, and highlight
halation is film-plane v3. Defaults still serialize to the prior schemas.
Saved shots, exports and camera paths preserve the new settings. Color values
interpolate in paths; incompatible pupil shapes are rejected between path shots.

Focused GPU checks cover neutral identity, warmth/tint/vibrance/fade response,
cached optical reuse, texture parity, threshold response, tiled halation and
rendered oval bokeh. The measured synthetic bokeh aspect was 1.990 versus 0.996
for round; integrated brightness ratio was 0.981. Existing independent tone and
film-plane references passed unchanged.

The native studio workflow exercises pointer/keyboard focus controls, fine
focus, starting-point selection, highlight cutoff, color changes, saved-shot
recall, inspector resizing, compact geometry and workspace recreation. It passed
with no QML warnings. These checks do not establish photographic stock matching
or end-to-end latency on every dense capture.

The broader UX still needs per-adjustment undo/compare, visual light-placement
tools and a clearer distinction between scene-wide composition and per-shot
camera recipes. Physical shutter/ISO, full lens transport and measured film /
scanner profiles require renderer work rather than additional inert controls.
