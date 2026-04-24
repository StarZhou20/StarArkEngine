#pragma once

#include <cstdint>

namespace ark {

/// Aggregated render-tuning parameters. Owned by `ForwardRenderer`, exposed
/// via `GetRenderSettings()`. Intended endpoint for future JSON scene
/// serialization (Phase M10) — all values here are per-scene tunables,
/// never hardware capabilities.
struct RenderSettings {
    // --- Global exposure (linear multiplier before ACES) -----------------
    float exposure = 1.0f;

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
        float intensity = 1.0f;
    } sky;

    // --- Image-based lighting (Phase 12) ---------------------------------
    struct {
        bool  enabled          = true;
        float diffuseIntensity = 1.0f;   // Scales irradiance term
        float specularIntensity = 1.0f;  // Scales prefilter + BRDF term
    } ibl;

    // --- Directional shadow map (Phase 13) -------------------------------
    struct {
        bool  enabled       = true;
        int   resolution    = 2048;   // Depth texture size (square)
        float orthoHalfSize = 25.0f;  // World-space half-extent of light frustum
        float nearPlane     = 0.1f;
        float farPlane      = 100.0f;
        float depthBias     = 0.002f; // Constant bias
        float normalBias    = 0.010f; // N·L-scaled bias to fight self-shadow acne
        int   pcfKernel     = 2;      // (2*k+1)^2 PCF taps; 0=1x1, 1=3x3, 2=5x5
    } shadow;
};

} // namespace ark
