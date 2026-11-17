.pragma library

// Editable creative starting points, not measured commercial stocks or glass.
var filmLooks = [
    {key: "clean", label: "Clean digital", grain: 0, scale: 8, halo: 0, diffusion: 60,
        threshold: 0.6, warmth: 0, tint: 0, vibrance: 0, fade: 0, contrast: 1, shoulder: 0, saturation: 1},
    {key: "daylight", label: "Soft daylight negative", grain: 0.25, scale: 8, halo: 0.15, diffusion: 70,
        threshold: 0.5, warmth: 0.16, tint: 0.02, vibrance: 0.08, fade: 0.015, contrast: 0.96, shoulder: 0.3, saturation: 0.95},
    {key: "tungsten", label: "Tungsten night / red glow", grain: 0.38, scale: 11, halo: 0.75, diffusion: 125,
        threshold: 0.25, warmth: -0.18, tint: 0.06, vibrance: 0.1, fade: 0.01, contrast: 1.05, shoulder: 0.35, saturation: 0.94},
    {key: "chrome", label: "Vivid slide", grain: 0.12, scale: 6, halo: 0.08, diffusion: 40,
        threshold: 0.65, warmth: -0.05, tint: 0.02, vibrance: 0.28, fade: 0, contrast: 1.15, shoulder: 0.15, saturation: 1.1},
    {key: "faded", label: "Faded warm print", grain: 0.4, scale: 14, halo: 0.3, diffusion: 95,
        threshold: 0.4, warmth: 0.28, tint: 0.06, vibrance: -0.15, fade: 0.055, contrast: 0.85, shoulder: 0.25, saturation: 0.85},
    {key: "mono", label: "Classic monochrome", grain: 0.45, scale: 15, halo: 0.08, diffusion: 60,
        threshold: 0.5, warmth: 0, tint: 0, vibrance: 0, fade: 0.01, contrast: 1.12, shoulder: 0.2, saturation: 0}
]
var lensLooks = [
    {key: "clean", label: "Clean spherical", aspect: 1, blades: 0, roundness: 0, edge: 0,
        distortion: 0, astigmatism: 0, curvature: 0, bloom: 0, spread: 8, flare: 0, rays: 4, length: 32, fringe: 0},
    {key: "oval", label: "Anamorphic character / 2:1 oval", aspect: 2, blades: 9, roundness: 0.9, edge: 0,
        distortion: 0, astigmatism: 0, curvature: 0, bloom: 0.18, spread: 12, flare: 1.2, rays: 1, length: 64, fringe: 0.25},
    {key: "vintage", label: "Vintage soft lens", aspect: 1, blades: 6, roundness: 0.2, edge: 1,
        distortion: -0.08, astigmatism: 0.2, curvature: 0.15, bloom: 0.3, spread: 14, flare: 0.2, rays: 6, length: 24, fringe: 0.5},
    {key: "mist", label: "Soft diffusion", aspect: 1, blades: 0, roundness: 0, edge: 0,
        distortion: 0, astigmatism: 0, curvature: 0, bloom: 0.65, spread: 18, flare: 0, rays: 4, length: 32, fringe: 0},
    {key: "star", label: "Starburst highlights", aspect: 1, blades: 6, roundness: 0, edge: 0,
        distortion: 0, astigmatism: 0, curvature: 0, bloom: 0.08, spread: 6, flare: 0.85, rays: 6, length: 48, fringe: 0}
]
function find(items, key) {
    for (var i = 0; i < items.length; ++i) if (items[i].key === key) return items[i]
    return null
}
