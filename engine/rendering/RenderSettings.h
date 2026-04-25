#pragma once

#include <cstdint>

namespace ark {

/// Aggregated render-tuning parameters. Owned by `ForwardRenderer`, exposed
/// via `GetRenderSettings()`. Intended endpoint for future JSON scene
/// serialization (Phase M10) — all values here are per-scene tunables,
/// never hardware capabilities.
struct RenderSettings {
    // --- Global exposure (linear multiplier before ACES) -----------------
    float exposure = 1.30f;

    // --- Bloom (Phase 10) -------------------------------------------------
    struct {
        bool  enabled    = true;
        float threshold  = 1.0f;   // HDR linear-space knee
        float strength   = 0.6f;   // Composite multiplier
        int   iterations = 5;      // Gaussian ping-pong pairs
    } bloom;

    // --- Skybox (Phase 11) ------------------------------------------------
    struct {
        bool  enabled   = true;
        float intensity = 0.7f;
    } sky;

    // --- Image-based lighting (Phase 12) ---------------------------------
    // IBL defaults were originally both 1.0 which with a bright gradient
    // skybox produces a flat, overcast look that drowns out the directional
    // light. Lower diffuse (ambient) aggressively, keep specular moderate
    // so metals still read as metallic.
    struct {
        bool  enabled          = true;
        float diffuseIntensity  = 0.30f;  // Lower ambient -> stronger sun/shadow contrast
        float specularIntensity = 0.55f;
    } ibl;

    // --- Directional shadow map (Phase 13) -------------------------------
    struct {
        bool  enabled       = true;
        int   resolution    = 4096;   // Depth texture size (square)
        float orthoHalfSize = 80.0f;  // 80m halfSize -> 4cm texel @ 4096 (sharp near shadow)
        float nearPlane     = -120.0f;
        float farPlane      = 250.0f;
        // World-space normal-offset along N; tiny because shadow texel is now ~4cm.
        // depthBias is NDC depth [0,1]; over 370m range, 0.00015 ≈ 5cm.
        float depthBias     = 0.00015f;
        float normalBias    = 0.04f;
        int   pcfKernel     = 1;       // filter radius (1+0.5)*texel = ~6cm (small features survive)
    } shadow;

    // --- Screen-space ambient occlusion (Phase 14) -----------------------
    struct {
        bool  enabled   = true;
        float intensity = 2.5f;   // 1 = linear AO, >1 darker / more contact shadow
        float radius    = 0.20f;  // Smaller radius -> reads thin contact (pole bases)
        float bias      = 0.020f; // Depth compare bias to reduce self-occlusion
        int   samples   = 32;     // Hemisphere samples (max 64)
    } ssao;

    // --- Contact shadows (Phase 15) --------------------------------------
    // Short-range screen-space ray-march toward the main directional light,
    // adds crisp contact shadows that shadow maps miss (small objects, seams).
    // Disabled by default: single-step ray-march is noisy on thin geometry
    // (foliage, wires) and the extra blur passes only partially hide it.
    struct {
        bool  enabled     = false;  // Off by default: noisy on curved geometry (arches)
        int   steps       = 16;     // Ray-march iterations per pixel
        float maxDistance = 0.25f;  // View-space march length (meters)
        float thickness   = 0.05f;  // Depth-buffer thickness tolerance
        float strength    = 0.7f;   // 1 = full shadow, 0 = disabled
    } contactShadow;

    // --- Anti-aliasing (Phase 15) ----------------------------------------
    struct {
        bool  enabled = true;       // FXAA 3.11
    } fxaa;

    // --- Temporal AA (Phase 16) ------------------------------------------
    // History-based AA driven by camera motion (no projection jitter).
    // When enabled, runs *instead* of FXAA (mutually exclusive). Best for
    // killing edge crawl / specular shimmer during view rotation.
    struct {
        bool  enabled    = false;   // disabled: TAA destabilises bright HDR point
                                    // sources (string lights -> coloured smears).
                                    // FXAA covers edge AA in the meantime.
        float blendNew   = 0.10f;   // 0.05 = sticky/blurry, 0.20 = sharp/noisy
    } taa;

    // --- Screen-space reflections (Phase 16) ----------------------------
    // Depth-only SSR using normals reconstructed from depth derivatives.
    // Only applies to surfaces facing roughly upward (floors, wet ground).
    struct {
        bool  enabled     = false;  // [DEBUG STEP 1] off to isolate banding source
        float maxDistance = 8.0f;
        int   steps       = 32;
        float thickness   = 0.5f;   // Depth slab tolerance for hit accept
        float fadeEdge    = 0.1f;   // UV-space fade band near screen edges
    } ssr;

    // --- Tone mapping (Phase 15) -----------------------------------------
    // 0 = ACES Fitted, 1 = AgX (Minimal). ACES is contrastier and matches
    // the UE5/Unity HDRP look out of the box; AgX is softer/more neutral.
    struct {
        int mode = 0;  // 0 = ACES (default, higher contrast)
    } tonemap;

    // --- MSAA (Phase 15) -------------------------------------------------
    // Hardware multisample on the HDR scene FBO. Resolved to a single-
    // sample texture before post-processing. 0 or 1 disables MSAA.
    struct {
        int samples = 4;   // 0/1 = off, 2/4/8 supported
    } msaa;

    // --- Height fog (Phase 16) -------------------------------------------
    // Exponential height-fog applied in the composite pass. Density falls
    // off with world-space Y, so air is denser near the ground and thins
    // upward — gives the scene atmosphere without flattening sky.
    struct {
        bool  enabled       = true;
        // Subtle "atmospheric haze" rather than fog. Very low values keep
        // contrast/colour intact while still selling depth.
        float density       = 0.0010f;   // was 0.0025 -> distant buildings keep detail
        float heightStart   = 0.0f;
        float heightFalloff = 0.05f;
        float maxOpacity    = 0.10f;     // was 0.20 -> cap haze at 10%
        float color[3]      = { 0.70f, 0.74f, 0.78f };
    } fog;
};

} // namespace ark
