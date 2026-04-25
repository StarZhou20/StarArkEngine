#include "PostProcess.h"
#include "engine/debug/DebugListenBus.h"

#include <GL/glew.h>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <cmath>

namespace ark {

// ---------- embedded shader sources ----------------------------------------

namespace {

const char* kFullscreenVS = R"(#version 450 core
// Fullscreen triangle: 3-vert no-attribute pass (we still feed a VBO so
// the driver is happy, but position/uv are derived from gl_VertexID when
// possible). Here we use a minimal per-vertex attribute for clarity.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Extracts pixels whose luminance exceeds a threshold, for the bloom chain.
const char* kBrightFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
uniform float uSoftKnee; // 0..1

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float brightness = dot(c, vec3(0.2126, 0.7152, 0.0722));

    // Soft-knee curve around uThreshold.
    float knee = uThreshold * uSoftKnee;
    float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, brightness - uThreshold);
    contribution /= max(brightness, 1e-4);

    FragColor = vec4(c * contribution, 1.0);
}
)";

// Separable 9-tap gaussian blur (horizontal if uHorizontal != 0, else vertical).
const char* kBlurFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform int uHorizontal;

const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uTex, 0));
    vec3 result = texture(uTex, vUV).rgb * weights[0];

    if (uHorizontal != 0) {
        for (int i = 1; i < 5; ++i) {
            result += texture(uTex, vUV + vec2(texel.x * float(i), 0.0)).rgb * weights[i];
            result += texture(uTex, vUV - vec2(texel.x * float(i), 0.0)).rgb * weights[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += texture(uTex, vUV + vec2(0.0, texel.y * float(i))).rgb * weights[i];
            result += texture(uTex, vUV - vec2(0.0, texel.y * float(i))).rgb * weights[i];
        }
    }
    FragColor = vec4(result, 1.0);
}
)";

// Composite: adds bloom to scene, applies exposure, ACES tone map.
// NOTE: output is **linear**; the default framebuffer has GL_FRAMEBUFFER_SRGB
// enabled (Window.cpp) and performs linear→sRGB encoding on write. Emitting
// sRGB-encoded values here would double-gamma and wash out the image.
const char* kCompositeFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uAO;
uniform sampler2D uContact;
uniform sampler2D uDepth;
uniform sampler2D uSSR;
uniform float uExposure;
uniform float uBloomStrength;
uniform int uBloomEnabled;
uniform int uSSAOEnabled;
uniform int uContactEnabled;
uniform int uTonemap;   // 0 = ACES, 1 = AgX
uniform int uSSREnabled;
uniform float uSSRStrength;

// Height-fog
uniform int   uFogEnabled;
uniform mat4  uInvViewProj;
uniform vec3  uCameraPos;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogHeightStart;
uniform float uFogHeightFalloff;
uniform float uFogMaxOpacity;

// ACES filmic tone mapper (Stephen Hill fit). Linear HDR in, linear LDR out.
const mat3 kACESInput = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);
const mat3 kACESOutput = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 c) {
    c = kACESInput  * c;
    c = RRTAndODTFit(c);
    c = kACESOutput * c;
    return clamp(c, 0.0, 1.0);
}

// ---- AgX Minimal (by bwrensch / Troy Sobotka) ----------------------------
// Ported from the public-domain "Minimal AgX" fit. Produces softer highlight
// roll-off and a slightly desaturated filmic look vs. ACES.
const mat3 kAgxInset = mat3(
    0.842479062253094, 0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772, 0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
const mat3 kAgxOutset = mat3(
     1.19687900512017, -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
    -0.0990297440797205, -0.0989611768448433, 1.15107367264116);

vec3 AgxDefaultContrast(vec3 x) {
    // 6th-order polynomial sigmoid approximation.
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2
         - 40.14 * x4 * x
         + 31.96 * x4
         - 6.868 * x2 * x
         + 0.4298 * x2
         + 0.1191 * x
         - 0.00232;
}

vec3 AgX(vec3 c) {
    // AgX expects log2 encoded values in [-12.47, 4.026] normalized to [0,1].
    const float minEv = -12.47393;
    const float maxEv =   4.026069;
    c = kAgxInset * max(c, vec3(0.0));
    c = clamp(log2(max(c, vec3(1e-10))), minEv, maxEv);
    c = (c - minEv) / (maxEv - minEv);
    c = AgxDefaultContrast(c);
    c = kAgxOutset * c;
    // Convert back to linear for the sRGB framebuffer encode.
    c = pow(max(c, vec3(0.0)), vec3(2.2));
    return clamp(c, 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    // SSAO multiplies scene radiance before bloom so the bloom chain sees
    // the already-occluded image (prevents bloom from "filling in" dark
    // crevices that should stay dark).
    if (uSSAOEnabled != 0) {
        float ao = texture(uAO, vUV).r;
        // Mild darkening curve + generous floor so crevices read as shaded
        // without producing hard black "ink" outlines on silhouettes.
        ao = pow(clamp(ao, 0.0, 1.0), 1.3);
        ao = mix(0.35, 1.0, ao);
        hdr *= ao;
    }
    if (uContactEnabled != 0) {
        float cs = texture(uContact, vUV).r;
        // cs is already in [0,1] with 1 = unshadowed; apply a gentle floor
        // so full occlusion still receives some indirect light.
        cs = mix(0.25, 1.0, cs);
        hdr *= cs;
    }
    if (uSSREnabled != 0) {
        // SSR texture: rgb = reflected radiance, a = mask. Add it on top of
        // the surface (additive — mask already encodes upward + fresnel +
        // edge fades), scaled by a global strength uniform.
        vec4 ssr = texture(uSSR, vUV);
        hdr += ssr.rgb * ssr.a * uSSRStrength;
    }
    if (uBloomEnabled != 0) {
        hdr += texture(uBloom, vUV).rgb * uBloomStrength;
    }

    // Height-exponential fog: denser at low altitude, thins upward. We
    // approximate the line integral of an exp(-falloff*y) density between
    // the camera and the surface using the standard analytic form
    // F = density / falloff * (exp(-falloff*ya) - exp(-falloff*yb)) / (yb-ya)
    // multiplied by world-space distance.
    if (uFogEnabled != 0) {
        float depth = texture(uDepth, vUV).r;
        if (depth < 1.0) {
            vec4 ndc   = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            vec4 wpos4 = uInvViewProj * ndc;
            vec3 wpos  = wpos4.xyz / wpos4.w;
            vec3 ray   = wpos - uCameraPos;
            float dist = length(ray);
            float ya   = uCameraPos.y - uFogHeightStart;
            float yb   = wpos.y       - uFogHeightStart;
            float dy   = yb - ya;
            float fInt;
            if (abs(dy) > 1e-3) {
                fInt = (exp(-uFogHeightFalloff * ya) - exp(-uFogHeightFalloff * yb))
                     / (uFogHeightFalloff * dy);
            } else {
                fInt = exp(-uFogHeightFalloff * ya);
            }
            float optical = uFogDensity * dist * max(fInt, 0.0);
            float fog     = 1.0 - exp(-optical);
            fog           = clamp(fog, 0.0, uFogMaxOpacity);
            hdr           = mix(hdr, uFogColor, fog);
        }
    }

    hdr *= uExposure;
    vec3 ldr = (uTonemap == 0) ? ACESFitted(hdr) : AgX(hdr);
    FragColor = vec4(ldr, 1.0);
}
)";

// ---------- SSAO shaders ---------------------------------------------------

// Depth-only SSAO: reconstructs view-space position from the depth texture,
// reconstructs a normal from depth derivatives, then samples a randomly
// rotated hemisphere kernel in view space. Outputs a single-channel AO
// factor in [0,1] (1 = unoccluded).
const char* kSSAOFS = R"(#version 450 core
in vec2 vUV;
out float FragAO;

uniform sampler2D uDepth;
uniform sampler2D uNoise;
uniform vec3  uKernel[64];
uniform int   uKernelSize;
uniform mat4  uProjection;
uniform mat4  uInvProjection;
uniform vec2  uNoiseScale;  // screenSize / 4
uniform float uRadius;
uniform float uBias;
uniform float uIntensity;

vec3 ViewPosFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInvProjection * clip;
    return view.xyz / view.w;
}

void main() {
    float depth = texture(uDepth, vUV).r;
    // Skybox / cleared pixels = fully unoccluded.
    if (depth >= 0.999999) { FragAO = 1.0; return; }

    vec3 viewPos = ViewPosFromDepth(vUV, depth);

    // Reconstruct view-space normal from depth derivatives. Good enough for
    // SSAO; not meant to replace a proper GBuffer normal.
    vec3 n = normalize(cross(dFdx(viewPos), dFdy(viewPos)));

    // Random rotation around the normal (Crytek-style).
    vec3 randomVec = vec3(texture(uNoise, vUV * uNoiseScale).rg, 0.0);
    vec3 tangent   = normalize(randomVec - n * dot(randomVec, n));
    vec3 bitangent = cross(n, tangent);
    mat3 TBN = mat3(tangent, bitangent, n);

    float occlusion = 0.0;
    int N = uKernelSize;
    for (int i = 0; i < N; ++i) {
        vec3 samplePos = TBN * uKernel[i];
        samplePos = viewPos + samplePos * uRadius;

        // Project sample back to screen UV.
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        if (offset.x < 0.0 || offset.x > 1.0 ||
            offset.y < 0.0 || offset.y > 1.0) continue;

        float sampleDepth = texture(uDepth, offset.xy).r;
        vec3  sampleView  = ViewPosFromDepth(offset.xy, sampleDepth);

        // Range check: fade out when sample is far from the shaded point,
        // so silhouette pixels against the sky/background don't grow a
        // black halo. Using smoothstep with a soft ramp removes the
        // hard "outline" artifact around foreground objects.
        float dz = abs(viewPos.z - sampleView.z);
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(dz, 1e-4));
        // Extra near-depth falloff: if the depth gap is huge, this sample
        // is meaningless and we drop it entirely.
        rangeCheck *= 1.0 - smoothstep(uRadius * 2.0, uRadius * 4.0, dz);
        occlusion += (sampleView.z >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }
    occlusion = 1.0 - (occlusion / float(max(N, 1)));
    // Intensity: <1 lighter AO, >1 darker AO.
    FragAO = pow(clamp(occlusion, 0.0, 1.0), uIntensity);
}
)";

// 4x4 box blur for the SSAO target; removes the per-pixel noise introduced
// by the random rotation vector.
const char* kSSAOBlurFS = R"(#version 450 core
in vec2 vUV;
out float FragAO;
uniform sampler2D uAO;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uAO, 0));
    float sum = 0.0;
    // 5x5 box blur (25 taps) - wider than 4x4 to fully smooth the
    // stochastic AO/contact-shadow noise on smooth surfaces.
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            sum += texture(uAO, vUV + vec2(float(x), float(y)) * texel).r;
        }
    }
    FragAO = sum / 25.0;
}
)";

// ---------- Contact shadow -------------------------------------------------
// Short screen-space ray-march from each pixel toward the main directional
// light. Catches small-scale contact occlusion (objects touching the floor,
// foliage self-shadowing, crevices) that the shadow map's resolution misses.
const char* kContactFS = R"(#version 450 core
in vec2 vUV;
out float FragAO;

uniform sampler2D uDepth;
uniform mat4  uProjection;
uniform mat4  uInvProjection;
uniform vec3  uLightDirView;   // view-space direction TOWARD the light
uniform int   uSteps;
uniform float uMaxDistance;
uniform float uThickness;
uniform float uStrength;

vec3 ViewPosFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInvProjection * clip;
    return view.xyz / view.w;
}

float Hash12(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 0.999999) { FragAO = 1.0; return; }

    vec3 viewPos  = ViewPosFromDepth(vUV, depth);
    vec3 rayDir   = normalize(uLightDirView);

    // Per-pixel jitter on step phase. Keep small (< stepLen) so neighbours
    // sample similar depths -> blur can fully smooth the residual.
    float jitter  = Hash12(gl_FragCoord.xy);

    float stepLen = uMaxDistance / float(max(uSteps, 1));
    // Start a tiny constant offset off the surface to avoid self-hit.
    vec3 rayStart = viewPos + rayDir * (stepLen * 0.5);

    float occlusion = 0.0;

    for (int i = 0; i < uSteps; ++i) {
        // Phase = (i + jitter) gives stratified jitter; samples are
        // distributed evenly along the ray instead of clustering.
        float t = stepLen * (float(i) + jitter);
        vec3 samplePos = rayStart + rayDir * t;

        vec4 clip = uProjection * vec4(samplePos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv  = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        float sceneDepth = texture(uDepth, uv).r;
        vec3  scenePos   = ViewPosFromDepth(uv, sceneDepth);

        float dz = scenePos.z - samplePos.z;
        if (dz > 0.001 && dz < uThickness) {
            // Distance falloff so far hits attenuate.
            occlusion = 1.0 - (t / uMaxDistance);
            break;
        }
    }

    FragAO = 1.0 - occlusion * uStrength;
}
)";

// ---------- SSR (depth-only, reconstructed normals) -----------------------
// Reflects view-space rays from each pixel and marches in screen space until
// it hits the depth buffer (or runs out of budget). Outputs reflected
// radiance + alpha (mask) into a half-res RGBA16F target.
//
// Key restrictions:
//  * Only surfaces facing roughly upward (world +Y) reflect — keeps
//    SSR limited to floors / wet ground, where it has highest payoff and
//    fewest artifacts.
//  * Faded near screen edges and behind-camera rays.
//  * Single-step march with a quick refine; no roughness blur. Composite
//    can blend with a global strength uniform.
const char* kSSRFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform mat4  uProjection;
uniform mat4  uInvProjection;
uniform float uMaxDistance;
uniform int   uSteps;
uniform float uThickness;
uniform float uFadeEdge;

vec3 ViewPosFromUV(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 v   = uInvProjection * ndc;
    return v.xyz / v.w;
}

void main() {
    float depth0 = texture(uDepth, vUV).r;
    if (depth0 >= 1.0) { FragColor = vec4(0.0); return; }

    vec3 P  = ViewPosFromUV(vUV, depth0);

    // Reconstruct view-space normal from depth derivatives.
    vec3 dPdx = dFdx(P);
    vec3 dPdy = dFdy(P);
    vec3 N    = normalize(cross(dPdx, dPdy));
    // Faces-toward-camera: in view space the camera looks down -Z, so
    // surfaces visible to the camera have N.z > 0 after the cross product.
    if (N.z < 0.0) N = -N;

    // We only want SSR on surfaces facing world +Y (floors). Without
    // an inverse-view, approximate using view-space N.y > 0.6 — true when
    // the camera looks roughly horizontally over a flat surface. Loose
    // threshold is acceptable; composite multiplies by mask anyway.
    float upMask = smoothstep(0.55, 0.85, N.y);
    if (upMask <= 0.001) { FragColor = vec4(0.0); return; }

    // View direction (camera origin = (0,0,0) in view space).
    vec3 V = normalize(-P);
    // Reflection direction in view space.
    vec3 R = normalize(reflect(-V, N));

    // Skip rays going away from the screen.
    if (R.z >= 0.0) { FragColor = vec4(0.0); return; }

    // March ray. Step in view space, project to UV, sample scene depth,
    // compare to ray depth. On crossing, accept hit.
    vec3 rayStart = P + N * 0.05;          // bias to avoid self-hit
    vec3 rayEnd   = rayStart + R * uMaxDistance;

    // Project endpoints once; interpolate UV in screen space.
    vec4 ps = uProjection * vec4(rayStart, 1.0);
    vec4 pe = uProjection * vec4(rayEnd,   1.0);
    vec2 uvStart = (ps.xy / ps.w) * 0.5 + 0.5;
    vec2 uvEnd   = (pe.xy / pe.w) * 0.5 + 0.5;

    int steps = max(uSteps, 8);
    float stepF = 1.0 / float(steps);

    // Per-pixel hash jitter so adjacent pixels sample at different t-fractions.
    // Without this, uniform stepping produces visible banding parallel to the
    // horizon on grazing reflections (cobblestone ground at low camera angle).
    float jitter = fract(sin(dot(vUV, vec2(12.9898, 78.233))) * 43758.5453);
    float t = stepF * (0.5 + jitter);

    vec2  hitUV = vec2(-1.0);
    float hitT  = 0.0;
    float prevDz = -1.0e9;

    for (int i = 0; i < steps; ++i) {
        vec2 uv = mix(uvStart, uvEnd, t);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        vec3 rayP = mix(rayStart, rayEnd, t);

        float sceneDepth = texture(uDepth, uv).r;
        if (sceneDepth >= 1.0) { prevDz = -1.0e9; t += stepF; continue; }
        vec3 sceneP = ViewPosFromUV(uv, sceneDepth);

        float dz = sceneP.z - rayP.z;
        if (dz > 0.0 && dz < uThickness) {
            // Binary refine: search between previous and current t for a
            // tighter intersection. Removes most banding still left by jitter.
            float lo = t - stepF;
            float hi = t;
            for (int j = 0; j < 4; ++j) {
                float mid = 0.5 * (lo + hi);
                vec2  uvm = mix(uvStart, uvEnd, mid);
                vec3  rPm = mix(rayStart, rayEnd, mid);
                float sdm = texture(uDepth, uvm).r;
                if (sdm >= 1.0) { lo = mid; continue; }
                vec3  sPm = ViewPosFromUV(uvm, sdm);
                float dm  = sPm.z - rPm.z;
                if (dm > 0.0 && dm < uThickness) hi = mid;
                else                              lo = mid;
            }
            hitT  = hi;
            hitUV = mix(uvStart, uvEnd, hi);
            break;
        }
        prevDz = dz;
        t += stepF;
    }

    if (hitUV.x < 0.0) { FragColor = vec4(0.0); return; }

    // Edge fade: kill reflections that sample near screen borders.
    vec2  edge = min(hitUV, 1.0 - hitUV);
    float edgeFade = smoothstep(0.0, uFadeEdge, min(edge.x, edge.y));

    // Distance fade: weaker as ray gets longer.
    float distFade = 1.0 - hitT;

    // View-direction Fresnel (Schlick, F0=0.04 baseline). Glancing rays
    // reflect more — gives the wet-asphalt look.
    float NdotV  = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = 0.04 + (1.0 - 0.04) * pow(1.0 - NdotV, 5.0);

    float mask = upMask * edgeFade * distFade * fresnel;

    vec3 reflected = texture(uScene, hitUV).rgb;
    FragColor = vec4(reflected, mask);
}
)";

// ---------- FXAA (quality, with directional edge search) ------------------
// Based on Timothy Lottes' FXAA 3.11 high-quality preset. Does real edge
// endpoint search along the dominant gradient direction and computes a
// sub-pixel offset for the final sample. This is substantially stronger
// than a simple "blend toward 3x3 average" edge blur.
// Input is sRGB-stored (sampler auto-decodes to linear); luma uses a
// perceptual sqrt to keep edge detection robust across brightness ranges.
const char* kFXAAFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform vec2 uRcpFrame;  // 1/size

float Luma(vec3 c) { return sqrt(dot(c, vec3(0.299, 0.587, 0.114))); }

void main() {
    vec2 px = uRcpFrame;

    vec3 rgbM = texture(uScene, vUV).rgb;
    float lumaM = Luma(rgbM);
    float lumaN = Luma(texture(uScene, vUV + vec2(0.0, -px.y)).rgb);
    float lumaS = Luma(texture(uScene, vUV + vec2(0.0,  px.y)).rgb);
    float lumaW = Luma(texture(uScene, vUV + vec2(-px.x, 0.0)).rgb);
    float lumaE = Luma(texture(uScene, vUV + vec2( px.x, 0.0)).rgb);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range   = lumaMax - lumaMin;

    // Edge threshold: 0.0312 absolute (darks), 0.063*lumaMax relative.
    if (range < max(0.0312, lumaMax * 0.063)) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    float lumaNW = Luma(texture(uScene, vUV + vec2(-px.x, -px.y)).rgb);
    float lumaNE = Luma(texture(uScene, vUV + vec2( px.x, -px.y)).rgb);
    float lumaSW = Luma(texture(uScene, vUV + vec2(-px.x,  px.y)).rgb);
    float lumaSE = Luma(texture(uScene, vUV + vec2( px.x,  px.y)).rgb);

    // Sobel-like edge gradients.
    float edgeH = abs(lumaNW + lumaNE - 2.0 * lumaN)
                + abs(lumaW  + lumaE  - 2.0 * lumaM) * 2.0
                + abs(lumaSW + lumaSE - 2.0 * lumaS);
    float edgeV = abs(lumaNW + lumaSW - 2.0 * lumaW)
                + abs(lumaN  + lumaS  - 2.0 * lumaM) * 2.0
                + abs(lumaNE + lumaSE - 2.0 * lumaE);
    bool horz = edgeH >= edgeV;

    // Step direction (perpendicular to edge).
    float stepLen = horz ? px.y : px.x;
    float luma1   = horz ? lumaN : lumaW;
    float luma2   = horz ? lumaS : lumaE;
    float grad1   = luma1 - lumaM;
    float grad2   = luma2 - lumaM;
    bool  is1     = abs(grad1) >= abs(grad2);
    float gradientScaled = 0.25 * max(abs(grad1), abs(grad2));
    if (!is1) stepLen = -stepLen;

    // Starting UV for edge walk, shifted to the edge line.
    vec2 currentUV = vUV;
    if (horz) currentUV.y += stepLen * 0.5;
    else      currentUV.x += stepLen * 0.5;

    vec2 offset = horz ? vec2(px.x, 0.0) : vec2(0.0, px.y);
    vec2 uv1 = currentUV - offset;
    vec2 uv2 = currentUV + offset;

    float lumaLocal = 0.5 * (lumaM + (is1 ? luma1 : luma2));
    float lumaEnd1  = Luma(texture(uScene, uv1).rgb) - lumaLocal;
    float lumaEnd2  = Luma(texture(uScene, uv2).rgb) - lumaLocal;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    // March along the edge up to 10 steps with increasing stride.
    const float kQuality[10] = float[](1.0, 1.0, 1.0, 1.0, 1.0,
                                       1.5, 2.0, 2.0, 2.0, 4.0);
    for (int i = 0; i < 10; ++i) {
        if (reached1 && reached2) break;
        if (!reached1) {
            lumaEnd1 = Luma(texture(uScene, uv1).rgb) - lumaLocal;
            reached1 = abs(lumaEnd1) >= gradientScaled;
            if (!reached1) uv1 -= offset * kQuality[i];
        }
        if (!reached2) {
            lumaEnd2 = Luma(texture(uScene, uv2).rgb) - lumaLocal;
            reached2 = abs(lumaEnd2) >= gradientScaled;
            if (!reached2) uv2 += offset * kQuality[i];
        }
    }

    float dist1 = horz ? (vUV.x - uv1.x) : (vUV.y - uv1.y);
    float dist2 = horz ? (uv2.x - vUV.x) : (uv2.y - vUV.y);
    bool isDir1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLen = dist1 + dist2;
    float pixelOffset = -distFinal / edgeLen + 0.5;

    // Sign check: only shift toward the edge on the "inside" side. Getting
    // this wrong is what causes the classic FXAA white-halo on silhouettes.
    bool correct = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != (lumaM < lumaLocal);
    float finalOffset = correct ? pixelOffset : 0.0;
    // Hard cap so a mis-estimated edge cannot yank us more than half a pixel.
    finalOffset = clamp(finalOffset, 0.0, 0.5);

    // NOTE: the "sub-pixel aliasing" blend from FXAA 3.11 is intentionally
    // disabled here. In forward-with-HDR pipelines it frequently pulls in
    // high-luma neighbors (sky, specular) across silhouettes and paints a
    // visible bright rim, which is worse than the remaining aliasing.

    vec2 finalUV = vUV;
    if (horz) finalUV.y += finalOffset * stepLen;
    else      finalUV.x += finalOffset * stepLen;
    FragColor = vec4(texture(uScene, finalUV).rgb, 1.0);
}
)";

} // namespace

// ---------- ctor/dtor ------------------------------------------------------

PostProcess::PostProcess() = default;

PostProcess::~PostProcess() {
    ReleaseTargets();
    ReleasePrograms();
    if (ssaoNoiseTex_) { glDeleteTextures(1, &ssaoNoiseTex_); ssaoNoiseTex_ = 0; }
    if (fsVBO_) glDeleteBuffers(1, &fsVBO_);
    if (fsVAO_) glDeleteVertexArrays(1, &fsVAO_);
}

// ---------- resource management --------------------------------------------

void PostProcess::Init(int width, int height) {
    if (initialized_) return;

    // Fullscreen triangle geometry (covers clip space).
    static const float kTri[] = {
        // pos          uv
        -1.0f, -1.0f,   0.0f, 0.0f,
         3.0f, -1.0f,   2.0f, 0.0f,
        -1.0f,  3.0f,   0.0f, 2.0f,
    };
    glGenVertexArrays(1, &fsVAO_);
    glGenBuffers(1, &fsVBO_);
    glBindVertexArray(fsVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, fsVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kTri), kTri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    CompilePrograms();
    AllocTargets(width, height);

    initialized_ = true;
    ARK_LOG_INFO("Render",
        "PostProcess initialized (" + std::to_string(width) + "x" +
        std::to_string(height) + ", HDR+Bloom)");
}

void PostProcess::AllocTargets(int w, int h) {
    width_ = w;
    height_ = h;

    // HDR scene FBO: RGBA16F color + depth **texture** (sampleable for SSAO).
    glGenFramebuffers(1, &hdrFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);

    glGenTextures(1, &hdrColor_);
    glBindTexture(GL_TEXTURE_2D, hdrColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColor_, 0);

    glGenTextures(1, &hdrDepth_);
    glBindTexture(GL_TEXTURE_2D, hdrDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, hdrDepth_, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ARK_LOG_ERROR("Render", "PostProcess: HDR FBO incomplete");
    }

    // Optional multisample HDR target — scene renders here, we resolve into
    // hdrFBO_ in EndScene(). Uses renderbuffers (no sampling needed).
    msaaActive_ = 0;
    if (msaaSamples_ >= 2) {
        GLint maxSamples = 1;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        int s = msaaSamples_ > maxSamples ? maxSamples : msaaSamples_;

        glGenFramebuffers(1, &hdrMsFBO_);
        glBindFramebuffer(GL_FRAMEBUFFER, hdrMsFBO_);

        glGenRenderbuffers(1, &hdrMsColor_);
        glBindRenderbuffer(GL_RENDERBUFFER, hdrMsColor_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_RGBA16F, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, hdrMsColor_);

        glGenRenderbuffers(1, &hdrMsDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, hdrMsDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdrMsDepth_);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            msaaActive_ = s;
            ARK_LOG_INFO("Render", "PostProcess: MSAA " + std::to_string(s) + "x enabled");
        } else {
            ARK_LOG_ERROR("Render", "PostProcess: MSAA FBO incomplete, falling back");
            if (hdrMsFBO_)   { glDeleteFramebuffers(1, &hdrMsFBO_);   hdrMsFBO_   = 0; }
            if (hdrMsColor_) { glDeleteRenderbuffers(1, &hdrMsColor_); hdrMsColor_ = 0; }
            if (hdrMsDepth_) { glDeleteRenderbuffers(1, &hdrMsDepth_); hdrMsDepth_ = 0; }
        }
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    // Bloom ping-pong (half-res).
    const int bw = w / 2 > 0 ? w / 2 : 1;
    const int bh = h / 2 > 0 ? h / 2 : 1;
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &bloomFBO_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[i]);

        glGenTextures(1, &bloomColor_[i]);
        glBindTexture(GL_TEXTURE_2D, bloomColor_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomColor_[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ARK_LOG_ERROR("Render",
                std::string("PostProcess: bloom FBO[") + std::to_string(i) + "] incomplete");
        }
    }

    // SSAO ping-pong (full-res, single-channel R8). [0]=raw, [1]=blurred.
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &ssaoFBO_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO_[i]);

        glGenTextures(1, &ssaoColor_[i]);
        glBindTexture(GL_TEXTURE_2D, ssaoColor_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColor_[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ARK_LOG_ERROR("Render",
                std::string("PostProcess: SSAO FBO[") + std::to_string(i) + "] incomplete");
        }
    }

    // Contact shadow target (full-res R8).
    glGenFramebuffers(1, &contactFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, contactFBO_);
    glGenTextures(1, &contactTex_);
    glBindTexture(GL_TEXTURE_2D, contactTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, contactTex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ARK_LOG_ERROR("Render", "PostProcess: contact shadow FBO incomplete");
    }

    // SSR target (half-res RGBA16F): reflected radiance + alpha mask.
    const int sw = w / 2 > 0 ? w / 2 : 1;
    const int sh = h / 2 > 0 ? h / 2 : 1;
    glGenFramebuffers(1, &ssrFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssrFBO_);
    glGenTextures(1, &ssrColor_);
    glBindTexture(GL_TEXTURE_2D, ssrColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, sw, sh, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssrColor_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ARK_LOG_ERROR("Render", "PostProcess: SSR FBO incomplete");
    }

    // TAA history pair (sRGB8). Bound to the same FBO via attachment swap.
    glGenFramebuffers(1, &taaFBO_);
    for (int i = 0; i < 2; ++i) {
        glGenTextures(1, &taaHistory_[i]);
        glBindTexture(GL_TEXTURE_2D, taaHistory_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    taaWriteIndex_   = 0;
    taaHistoryValid_ = false;

    // LDR intermediate for FXAA (full-res sRGB8). Storing in sRGB gives us
    // gamma precision in the darks (avoids banding/noise) and, combined
    // with GL_FRAMEBUFFER_SRGB, makes the round-trip composite→FXAA→FB
    // gamma-correct: composite writes linear, GL encodes to sRGB on store;
    // FXAA sampler converts back to linear; FXAA writes linear to default
    // FB which encodes to sRGB on the final presentation.
    glGenFramebuffers(1, &ldrFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, ldrFBO_);
    glGenTextures(1, &ldrColor_);
    glBindTexture(GL_TEXTURE_2D, ldrColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ldrColor_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ARK_LOG_ERROR("Render", "PostProcess: LDR FBO incomplete");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::ReleaseTargets() {
    if (hdrFBO_)   { glDeleteFramebuffers(1, &hdrFBO_); hdrFBO_ = 0; }
    if (hdrColor_) { glDeleteTextures(1, &hdrColor_);   hdrColor_ = 0; }
    if (hdrDepth_) { glDeleteTextures(1, &hdrDepth_);   hdrDepth_ = 0; }
    for (int i = 0; i < 2; ++i) {
        if (bloomFBO_[i])   { glDeleteFramebuffers(1, &bloomFBO_[i]);   bloomFBO_[i] = 0; }
        if (bloomColor_[i]) { glDeleteTextures(1, &bloomColor_[i]);     bloomColor_[i] = 0; }
        if (ssaoFBO_[i])    { glDeleteFramebuffers(1, &ssaoFBO_[i]);    ssaoFBO_[i] = 0; }
        if (ssaoColor_[i])  { glDeleteTextures(1, &ssaoColor_[i]);      ssaoColor_[i] = 0; }
    }
    if (contactFBO_) { glDeleteFramebuffers(1, &contactFBO_); contactFBO_ = 0; }
    if (contactTex_) { glDeleteTextures(1, &contactTex_);     contactTex_ = 0; }
    if (ssrFBO_)     { glDeleteFramebuffers(1, &ssrFBO_);     ssrFBO_ = 0; }
    if (ssrColor_)   { glDeleteTextures(1, &ssrColor_);       ssrColor_ = 0; }
    if (taaFBO_)        { glDeleteFramebuffers(1, &taaFBO_);   taaFBO_ = 0; }
    if (taaHistory_[0]) { glDeleteTextures(1, &taaHistory_[0]); taaHistory_[0] = 0; }
    if (taaHistory_[1]) { glDeleteTextures(1, &taaHistory_[1]); taaHistory_[1] = 0; }
    taaHistoryValid_ = false;
    if (ldrFBO_)     { glDeleteFramebuffers(1, &ldrFBO_);     ldrFBO_ = 0; }
    if (ldrColor_)   { glDeleteTextures(1, &ldrColor_);       ldrColor_ = 0; }
    if (hdrMsFBO_)   { glDeleteFramebuffers(1, &hdrMsFBO_);   hdrMsFBO_ = 0; }
    if (hdrMsColor_) { glDeleteRenderbuffers(1, &hdrMsColor_); hdrMsColor_ = 0; }
    if (hdrMsDepth_) { glDeleteRenderbuffers(1, &hdrMsDepth_); hdrMsDepth_ = 0; }
    msaaActive_ = 0;
    width_ = height_ = 0;
}

void PostProcess::ResizeIfNeeded(int w, int h) {
    if (w == width_ && h == height_) return;
    if (w <= 0 || h <= 0) return;
    ReleaseTargets();
    AllocTargets(w, h);
    ARK_LOG_INFO("Render",
        "PostProcess resized to " + std::to_string(w) + "x" + std::to_string(h));
}

// ---------- TAA (motion-driven, neighborhood clamp) ------------------------
// Reprojects last frame's color using the previous view-projection matrix
// and the current frame's depth, blends a small fraction of the new sample
// into the history. Clipping the reprojected color to the 3x3 neighborhood
// AABB of the current frame eliminates ghosting on disocclusions.
//
// Operates in LDR (sRGB samplers auto-decode to linear). No projection
// jitter — temporal AA here is driven by camera motion, which is what the
// user notices as "edge crawl during movement".
const char* kTAAFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uCurrent;
uniform sampler2D uHistory;
uniform sampler2D uDepth;
uniform mat4  uPrevViewProj;
uniform mat4  uCurInvViewProj;
uniform vec2  uRcpFrame;
uniform float uBlendNew;       // 0.05..0.20
uniform int   uHistoryValid;   // 0 = first frame, just write current

vec3 RGBToYCoCg(vec3 c) {
    return vec3( 0.25*c.r + 0.5*c.g + 0.25*c.b,
                 0.5 *c.r           - 0.5 *c.b,
                -0.25*c.r + 0.5*c.g - 0.25*c.b);
}
vec3 YCoCgToRGB(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}

void main() {
    vec3 cur = texture(uCurrent, vUV).rgb;

    if (uHistoryValid == 0) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    // --- Closest-depth reprojection ----------------------------------------
    // Use the nearest depth in a 3x3 neighborhood. This avoids reprojecting
    // sub-pixel bright features (lamps, emissive bulbs) using background
    // depth, which is the main cause of "trail" ghosting on small lights.
    float depth = texture(uDepth, vUV).r;
    vec2  bestOff = vec2(0.0);
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        vec2 off = vec2(dx, dy) * uRcpFrame;
        float d  = texture(uDepth, vUV + off).r;
        if (d < depth) { depth = d; bestOff = off; }
    }

    vec2 sampleUV = vUV + bestOff;
    vec4 ndcCur   = vec4(sampleUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldH   = uCurInvViewProj * ndcCur;
    vec3 worldP   = worldH.xyz / worldH.w;
    vec4 prevClip = uPrevViewProj * vec4(worldP, 1.0);
    vec2 prevUV   = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    // Subtract back the offset so we read the history at the pixel-aligned
    // reprojection of THIS pixel (not the neighbour we picked depth from).
    prevUV -= bestOff;

    // If reprojected outside the screen, can't trust history.
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        FragColor = vec4(cur, 1.0);
        return;
    }

    vec3 hist = texture(uHistory, prevUV).rgb;

    // Build YCoCg neighborhood AABB on current. Variance clipping (tighter).
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    int  N  = 0;
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        vec3 s = texture(uCurrent, vUV + vec2(dx, dy) * uRcpFrame).rgb;
        s = RGBToYCoCg(s);
        m1 += s;
        m2 += s * s;
        ++N;
    }
    vec3 mean = m1 / float(N);
    vec3 var  = max(m2 / float(N) - mean * mean, vec3(0.0));
    vec3 sigma = sqrt(var) * 1.0;
    vec3 boxMin = mean - sigma;
    vec3 boxMax = mean + sigma;

    vec3 histY = RGBToYCoCg(hist);
    vec3 clampedY = clamp(histY, boxMin, boxMax);
    // Distance the history had to be moved to be admissible — drives
    // additional disocclusion weight: large clamp delta = stale history.
    float clampDist = length(clampedY - histY) /
                      max(length(boxMax - boxMin) * 0.5, 1e-4);
    hist  = YCoCgToRGB(clampedY);

    // Reduce history weight for fast disocclusions: if luma differs a lot
    // even after clamping, lean toward current. Stronger than before.
    float lumaCur  = dot(cur,  vec3(0.299, 0.587, 0.114));
    float lumaHist = dot(hist, vec3(0.299, 0.587, 0.114));
    float lumaTerm = clamp(abs(lumaCur - lumaHist) * 8.0, 0.0, 1.0);
    float disocclusion = clamp(max(lumaTerm, clampDist * 0.75), 0.0, 1.0);
    float a = mix(uBlendNew, 1.0, disocclusion);

    vec3 outC = mix(hist, cur, a);
    FragColor = vec4(outC, 1.0);
}
)";

// ---------- shader compilation ---------------------------------------------

uint32_t PostProcess::CompileProgram(const char* vs, const char* fs, const char* name) {
    auto compile = [&](GLenum stage, const char* src) -> GLuint {
        GLuint sh = glCreateShader(stage);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
            ARK_LOG_ERROR("Render",
                std::string("PostProcess(") + name + ") " +
                (stage == GL_VERTEX_SHADER ? "vs" : "fs") + " compile failed: " + log);
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    };

    GLuint v = compile(GL_VERTEX_SHADER,   vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        ARK_LOG_ERROR("Render",
            std::string("PostProcess(") + name + ") link failed: " + log);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

void PostProcess::CompilePrograms() {
    progBright_    = CompileProgram(kFullscreenVS, kBrightFS,    "bright");
    progBlur_      = CompileProgram(kFullscreenVS, kBlurFS,      "blur");
    progComposite_ = CompileProgram(kFullscreenVS, kCompositeFS, "composite");
    progSSAO_      = CompileProgram(kFullscreenVS, kSSAOFS,      "ssao");
    progSSAOBlur_  = CompileProgram(kFullscreenVS, kSSAOBlurFS,  "ssaoBlur");
    progContact_   = CompileProgram(kFullscreenVS, kContactFS,   "contactShadow");
    progFXAA_      = CompileProgram(kFullscreenVS, kFXAAFS,      "fxaa");
    progSSR_       = CompileProgram(kFullscreenVS, kSSRFS,       "ssr");
    progTAA_       = CompileProgram(kFullscreenVS, kTAAFS,       "taa");
}

void PostProcess::ReleasePrograms() {
    if (progBright_)    { glDeleteProgram(progBright_);    progBright_ = 0; }
    if (progBlur_)      { glDeleteProgram(progBlur_);      progBlur_ = 0; }
    if (progComposite_) { glDeleteProgram(progComposite_); progComposite_ = 0; }
    if (progSSAO_)      { glDeleteProgram(progSSAO_);      progSSAO_ = 0; }
    if (progSSAOBlur_)  { glDeleteProgram(progSSAOBlur_);  progSSAOBlur_ = 0; }
    if (progContact_)   { glDeleteProgram(progContact_);   progContact_ = 0; }
    if (progFXAA_)      { glDeleteProgram(progFXAA_);      progFXAA_ = 0; }
    if (progSSR_)       { glDeleteProgram(progSSR_);       progSSR_ = 0; }
    if (progTAA_)       { glDeleteProgram(progTAA_);       progTAA_ = 0; }
}

// ---------- per-frame ------------------------------------------------------

void PostProcess::SetMsaaSamples(int samples) {
    int req = samples <= 1 ? 0 : samples;
    if (req == msaaSamples_) return;
    msaaSamples_ = req;
    // Force realloc on next BeginScene by wiping dims.
    if (initialized_) {
        ReleaseTargets();
    }
}

void PostProcess::SetFog(bool enabled,
                         const float* invViewProj4x4,
                         const float* cameraPos3,
                         const float* color3,
                         float density,
                         float heightStart,
                         float heightFalloff,
                         float maxOpacity) {
    fogEnabled_ = enabled;
    if (invViewProj4x4) {
        for (int i = 0; i < 16; ++i) fogInvViewProj_[i] = invViewProj4x4[i];
    }
    if (cameraPos3) {
        fogCameraPos_[0] = cameraPos3[0];
        fogCameraPos_[1] = cameraPos3[1];
        fogCameraPos_[2] = cameraPos3[2];
    }
    if (color3) {
        fogColor_[0] = color3[0];
        fogColor_[1] = color3[1];
        fogColor_[2] = color3[2];
    }
    fogDensity_       = density;
    fogHeightStart_   = heightStart;
    fogHeightFalloff_ = heightFalloff;
    fogMaxOpacity_    = maxOpacity;
}

void PostProcess::BeginScene(int width, int height) {
    if (!initialized_) Init(width, height);
    ResizeIfNeeded(width, height);
    // If targets got wiped by SetMsaaSamples we need to realloc.
    if (hdrFBO_ == 0) AllocTargets(width, height);
    // Scene renders into MS buffer when active, otherwise straight into HDR.
    if (msaaActive_ >= 2 && hdrMsFBO_ != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, hdrMsFBO_);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
    }
    glViewport(0, 0, width_, height_);
}

void PostProcess::EndScene() {
    // Resolve MSAA color + depth into the single-sample hdrFBO_ so that
    // SSAO / bloom / composite can sample them as regular textures.
    if (msaaActive_ >= 2 && hdrMsFBO_ != 0 && hdrFBO_ != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, hdrMsFBO_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdrFBO_);
        glBlitFramebuffer(0, 0, width_, height_,
                          0, 0, width_, height_,
                          GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------- SSAO -----------------------------------------------------------

void PostProcess::EnsureSSAOResources() {
    if (ssaoNoiseTex_ != 0 && ssaoKernelSize_ != 0) return;

    // Hemisphere kernel: 64 samples biased toward the origin.
    std::mt19937 rng(1337u);
    std::uniform_real_distribution<float> rand01(0.0f, 1.0f);
    const int N = 64;
    for (int i = 0; i < N; ++i) {
        float x = rand01(rng) * 2.0f - 1.0f;
        float y = rand01(rng) * 2.0f - 1.0f;
        float z = rand01(rng);                 // +z hemisphere
        float len = std::sqrt(x*x + y*y + z*z);
        if (len < 1e-5f) { x = 0; y = 0; z = 1; len = 1; }
        x /= len; y /= len; z /= len;
        float s = float(i) / float(N);
        s = 0.1f + s * s * 0.9f;               // accelerate toward center
        float r = rand01(rng) * s;
        ssaoKernel_[i*3 + 0] = x * r;
        ssaoKernel_[i*3 + 1] = y * r;
        ssaoKernel_[i*3 + 2] = z * r;
    }
    ssaoKernelSize_ = N;

    // 4x4 noise texture: random rotations in the tangent plane (xy).
    float noise[4*4*2];
    for (int i = 0; i < 16; ++i) {
        noise[i*2 + 0] = rand01(rng) * 2.0f - 1.0f;
        noise[i*2 + 1] = rand01(rng) * 2.0f - 1.0f;
    }
    glGenTextures(1, &ssaoNoiseTex_);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 4, 4, 0, GL_RG, GL_FLOAT, noise);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// 4x4 matrix inverse (column-major). We don't depend on glm here.
static void InvertMat4(const float* m, float* out) {
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9] *m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9] *m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9] *m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9] *m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6] *m[15] - m[1]*m[7] *m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6] *m[15] + m[0]*m[7] *m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5] *m[15] - m[0]*m[7] *m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5] *m[14] + m[0]*m[6] *m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6] *m[11] + m[1]*m[7] *m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9] *m[2]*m[7]  + m[9] *m[3]*m[6];
    inv[7]  =  m[0]*m[6] *m[11] - m[0]*m[7] *m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8] *m[2]*m[7]  - m[8] *m[3]*m[6];
    inv[11] = -m[0]*m[5] *m[11] + m[0]*m[7] *m[9]  + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8] *m[1]*m[7]  + m[8] *m[3]*m[5];
    inv[15] =  m[0]*m[5] *m[10] - m[0]*m[6] *m[9]  - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8] *m[1]*m[6]  - m[8] *m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (det == 0.0f) { for (int i = 0; i < 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f; return; }
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
}

void PostProcess::ApplySSAO(const float* projMat,
                            float intensity, float radius, float bias, int samples) {
    if (!initialized_ || !projMat) { ssaoValidThisFrame_ = false; return; }
    EnsureSSAOResources();

    if (samples < 4)  samples = 4;
    if (samples > 64) samples = 64;

    float invProj[16];
    InvertMat4(projMat, invProj);

    GLboolean depthWas = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depthWas);
    glDisable(GL_DEPTH_TEST);

    // --- Raw SSAO pass → ssaoFBO_[0] ---
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO_[0]);
    glViewport(0, 0, width_, height_);
    glUseProgram(progSSAO_);

    glUniform1i (glGetUniformLocation(progSSAO_, "uDepth"), 0);
    glUniform1i (glGetUniformLocation(progSSAO_, "uNoise"), 1);
    glUniformMatrix4fv(glGetUniformLocation(progSSAO_, "uProjection"),    1, GL_FALSE, projMat);
    glUniformMatrix4fv(glGetUniformLocation(progSSAO_, "uInvProjection"), 1, GL_FALSE, invProj);
    glUniform2f (glGetUniformLocation(progSSAO_, "uNoiseScale"),
                 float(width_) / 4.0f, float(height_) / 4.0f);
    glUniform1f (glGetUniformLocation(progSSAO_, "uRadius"),    radius);
    glUniform1f (glGetUniformLocation(progSSAO_, "uBias"),      bias);
    glUniform1f (glGetUniformLocation(progSSAO_, "uIntensity"), intensity);
    glUniform1i (glGetUniformLocation(progSSAO_, "uKernelSize"), samples);
    glUniform3fv(glGetUniformLocation(progSSAO_, "uKernel"),
                 ssaoKernelSize_, ssaoKernel_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrDepth_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex_);

    glBindVertexArray(fsVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- Blur pass → ssaoFBO_[1] ---
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO_[1]);
    glViewport(0, 0, width_, height_);
    glUseProgram(progSSAOBlur_);
    glUniform1i(glGetUniformLocation(progSSAOBlur_, "uAO"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoColor_[0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    if (depthWas) glEnable(GL_DEPTH_TEST);

    ssaoValidThisFrame_ = true;
}

void PostProcess::ApplyContactShadow(const float* projMat,
                                     const float* viewLightDir,
                                     int   steps,
                                     float maxDistance,
                                     float thickness,
                                     float strength) {
    if (!initialized_ || !projMat || !viewLightDir) {
        contactValidThisFrame_ = false;
        return;
    }

    if (steps < 4)   steps = 4;
    if (steps > 64)  steps = 64;

    float invProj[16];
    InvertMat4(projMat, invProj);

    GLboolean depthWas = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depthWas);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, contactFBO_);
    glViewport(0, 0, width_, height_);
    glUseProgram(progContact_);

    glUniform1i (glGetUniformLocation(progContact_, "uDepth"), 0);
    glUniformMatrix4fv(glGetUniformLocation(progContact_, "uProjection"),    1, GL_FALSE, projMat);
    glUniformMatrix4fv(glGetUniformLocation(progContact_, "uInvProjection"), 1, GL_FALSE, invProj);
    glUniform3fv(glGetUniformLocation(progContact_, "uLightDirView"), 1, viewLightDir);
    glUniform1i (glGetUniformLocation(progContact_, "uSteps"),       steps);
    glUniform1f (glGetUniformLocation(progContact_, "uMaxDistance"), maxDistance);
    glUniform1f (glGetUniformLocation(progContact_, "uThickness"),   thickness);
    glUniform1f (glGetUniformLocation(progContact_, "uStrength"),    strength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrDepth_);

    glBindVertexArray(fsVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- Blur pass: reuse the SSAO 4x4 box blur to remove ray-march noise.
    // The result lands in ssaoColor_[0] (free at this point since SSAO
    // already consumed/wrote [0]→[1] earlier this frame). The composite
    // pass reads from `ssaoColor_[0]` as the contact shadow texture.
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO_[0]);
    glViewport(0, 0, width_, height_);
    glUseProgram(progSSAOBlur_);
    glUniform1i(glGetUniformLocation(progSSAOBlur_, "uAO"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, contactTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    if (depthWas) glEnable(GL_DEPTH_TEST);

    contactValidThisFrame_ = true;
}

void PostProcess::ApplySSR(const float* projMat,
                           const float* invProjMat,
                           float intensity,
                           float maxDistance,
                           int   steps,
                           float thickness,
                           float fadeEdge) {
    (void)intensity; // strength is applied in composite
    if (!initialized_ || !progSSR_) return;

    GLboolean depthWas = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depthWas);
    glDisable(GL_DEPTH_TEST);

    const int sw = width_  / 2 > 0 ? width_  / 2 : 1;
    const int sh = height_ / 2 > 0 ? height_ / 2 : 1;
    glBindFramebuffer(GL_FRAMEBUFFER, ssrFBO_);
    glViewport(0, 0, sw, sh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(progSSR_);
    glUniform1i(glGetUniformLocation(progSSR_, "uScene"),         0);
    glUniform1i(glGetUniformLocation(progSSR_, "uDepth"),         1);
    glUniformMatrix4fv(glGetUniformLocation(progSSR_, "uProjection"),    1, GL_FALSE, projMat);
    glUniformMatrix4fv(glGetUniformLocation(progSSR_, "uInvProjection"), 1, GL_FALSE, invProjMat);
    glUniform1f(glGetUniformLocation(progSSR_, "uMaxDistance"), maxDistance);
    glUniform1i(glGetUniformLocation(progSSR_, "uSteps"),       steps);
    glUniform1f(glGetUniformLocation(progSSR_, "uThickness"),   thickness);
    glUniform1f(glGetUniformLocation(progSSR_, "uFadeEdge"),    fadeEdge);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColor_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hdrDepth_);

    glBindVertexArray(fsVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glUseProgram(0);
    if (depthWas) glEnable(GL_DEPTH_TEST);

    ssrValidThisFrame_ = true;
}

void PostProcess::Apply(int screenW, int screenH,
                        float exposure,
                        float bloomThreshold,
                        float bloomStrength,
                        int blurIterations,
                        bool ssaoEnabled,
                        bool contactShadowEnabled,
                        bool fxaaEnabled,
                        int  tonemapMode,
                        bool ssrEnabled,
                        bool taaEnabled,
                        float taaBlendNew,
                        const float* prevViewProj16,
                        const float* curInvViewProj16) {
    if (!initialized_) return;

    // Disable depth for all fullscreen passes.
    GLboolean depthWas = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depthWas);
    glDisable(GL_DEPTH_TEST);

    const int bw = width_  / 2 > 0 ? width_  / 2 : 1;
    const int bh = height_ / 2 > 0 ? height_ / 2 : 1;

    bool bloomActive   = bloomEnabled_ && bloomStrength > 0.0f && blurIterations > 0;
    bool aoActive      = ssaoEnabled && ssaoValidThisFrame_;
    bool contactActive = contactShadowEnabled && contactValidThisFrame_;
    bool ssrActive     = ssrEnabled && ssrValidThisFrame_;
    // TAA and FXAA both want the LDR intermediate; TAA takes precedence
    // (mutually exclusive in render settings, but enforce it here too).
    bool taaActive  = taaEnabled && progTAA_ != 0 && prevViewProj16 && curInvViewProj16;
    bool fxaaActive = fxaaEnabled && !taaActive;
    bool needsLdr   = taaActive || fxaaActive;

    if (bloomActive) {
        // --- Bright pass: HDR scene -> bloom[0] ---
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[0]);
        glViewport(0, 0, bw, bh);
        glUseProgram(progBright_);
        glUniform1i(glGetUniformLocation(progBright_, "uScene"), 0);
        glUniform1f(glGetUniformLocation(progBright_, "uThreshold"), bloomThreshold);
        glUniform1f(glGetUniformLocation(progBright_, "uSoftKnee"),  0.5f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColor_);
        glBindVertexArray(fsVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // --- Gaussian blur ping-pong on bloom chain ---
        glUseProgram(progBlur_);
        glUniform1i(glGetUniformLocation(progBlur_, "uTex"), 0);
        int src = 0;
        for (int i = 0; i < blurIterations * 2; ++i) {
            int dst = 1 - src;
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[dst]);
            glViewport(0, 0, bw, bh);
            glUniform1i(glGetUniformLocation(progBlur_, "uHorizontal"), (i & 1) == 0 ? 1 : 0);
            glBindTexture(GL_TEXTURE_2D, bloomColor_[src]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            src = dst;
        }

        // --- Composite: HDR + bloom -> LDR intermediate (for FXAA / TAA) ---
        const GLuint compositeTarget = needsLdr ? ldrFBO_ : 0u;
        glBindFramebuffer(GL_FRAMEBUFFER, compositeTarget);
        glViewport(0, 0, needsLdr ? width_ : screenW, needsLdr ? height_ : screenH);
        glUseProgram(progComposite_);
        glUniform1i(glGetUniformLocation(progComposite_, "uScene"),   0);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloom"),   1);
        glUniform1i(glGetUniformLocation(progComposite_, "uAO"),      2);
        glUniform1i(glGetUniformLocation(progComposite_, "uContact"), 3);
        glUniform1i(glGetUniformLocation(progComposite_, "uDepth"),   4);
        glUniform1f(glGetUniformLocation(progComposite_, "uExposure"),     exposure);
        glUniform1f(glGetUniformLocation(progComposite_, "uBloomStrength"), bloomStrength);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloomEnabled"), 1);
        glUniform1i(glGetUniformLocation(progComposite_, "uSSAOEnabled"),    aoActive      ? 1 : 0);
        glUniform1i(glGetUniformLocation(progComposite_, "uContactEnabled"), contactActive ? 1 : 0);
        glUniform1i(glGetUniformLocation(progComposite_, "uTonemap"),        tonemapMode);
        glUniform1i(glGetUniformLocation(progComposite_, "uSSREnabled"),     ssrActive ? 1 : 0);
        glUniform1f(glGetUniformLocation(progComposite_, "uSSRStrength"),    1.0f);
        glUniform1i(glGetUniformLocation(progComposite_, "uSSR"),            5);
        glUniform1i(glGetUniformLocation(progComposite_, "uFogEnabled"),     fogEnabled_ ? 1 : 0);
        glUniformMatrix4fv(glGetUniformLocation(progComposite_, "uInvViewProj"), 1, GL_FALSE, fogInvViewProj_);
        glUniform3fv(glGetUniformLocation(progComposite_, "uCameraPos"), 1, fogCameraPos_);
        glUniform3fv(glGetUniformLocation(progComposite_, "uFogColor"),  1, fogColor_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogDensity"),       fogDensity_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogHeightStart"),   fogHeightStart_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogHeightFalloff"), fogHeightFalloff_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogMaxOpacity"),    fogMaxOpacity_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColor_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomColor_[src]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, aoActive      ? ssaoColor_[1] : 0);
        glActiveTexture(GL_TEXTURE3);
        // Contact shadow final = blurred copy in ssaoColor_[0] (written
        // at the end of ApplyContactShadow). See note in that function.
        glBindTexture(GL_TEXTURE_2D, contactActive ? ssaoColor_[0]  : 0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, hdrDepth_);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, ssrActive ? ssrColor_ : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0);
    } else {
        // Bloom off: composite HDR directly to LDR intermediate (or FB).
        const GLuint compositeTarget = needsLdr ? ldrFBO_ : 0u;
        glBindFramebuffer(GL_FRAMEBUFFER, compositeTarget);
        glViewport(0, 0, needsLdr ? width_ : screenW, needsLdr ? height_ : screenH);
        glUseProgram(progComposite_);
        glUniform1i(glGetUniformLocation(progComposite_, "uScene"),   0);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloom"),   1);
        glUniform1i(glGetUniformLocation(progComposite_, "uAO"),      2);
        glUniform1i(glGetUniformLocation(progComposite_, "uContact"), 3);
        glUniform1i(glGetUniformLocation(progComposite_, "uDepth"),   4);
        glUniform1f(glGetUniformLocation(progComposite_, "uExposure"),     exposure);
        glUniform1f(glGetUniformLocation(progComposite_, "uBloomStrength"), 0.0f);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloomEnabled"), 0);
        glUniform1i(glGetUniformLocation(progComposite_, "uSSAOEnabled"),    aoActive      ? 1 : 0);
        glUniform1i(glGetUniformLocation(progComposite_, "uContactEnabled"), contactActive ? 1 : 0);
        glUniform1i(glGetUniformLocation(progComposite_, "uTonemap"),        tonemapMode);
        glUniform1i(glGetUniformLocation(progComposite_, "uSSREnabled"),     ssrActive ? 1 : 0);
        glUniform1f(glGetUniformLocation(progComposite_, "uSSRStrength"),    1.0f);
        glUniform1i(glGetUniformLocation(progComposite_, "uSSR"),            5);
        glUniform1i(glGetUniformLocation(progComposite_, "uFogEnabled"),     fogEnabled_ ? 1 : 0);
        glUniformMatrix4fv(glGetUniformLocation(progComposite_, "uInvViewProj"), 1, GL_FALSE, fogInvViewProj_);
        glUniform3fv(glGetUniformLocation(progComposite_, "uCameraPos"), 1, fogCameraPos_);
        glUniform3fv(glGetUniformLocation(progComposite_, "uFogColor"),  1, fogColor_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogDensity"),       fogDensity_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogHeightStart"),   fogHeightStart_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogHeightFalloff"), fogHeightFalloff_);
        glUniform1f(glGetUniformLocation(progComposite_, "uFogMaxOpacity"),    fogMaxOpacity_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColor_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, aoActive      ? ssaoColor_[1] : 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, contactActive ? ssaoColor_[0] : 0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, hdrDepth_);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, ssrActive ? ssrColor_ : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(fsVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    if (taaActive) {
        // --- TAA: ldrColor_ + history -> taaHistory_[write] -> default FB ---
        const int wIdx = taaWriteIndex_;
        const int rIdx = 1 - wIdx;

        // Pass 1: write blended color into the ping-pong history texture.
        glBindFramebuffer(GL_FRAMEBUFFER, taaFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, taaHistory_[wIdx], 0);
        glViewport(0, 0, width_, height_);
        glUseProgram(progTAA_);
        glUniform1i(glGetUniformLocation(progTAA_, "uCurrent"), 0);
        glUniform1i(glGetUniformLocation(progTAA_, "uHistory"), 1);
        glUniform1i(glGetUniformLocation(progTAA_, "uDepth"),   2);
        glUniformMatrix4fv(glGetUniformLocation(progTAA_, "uPrevViewProj"),
                           1, GL_FALSE, prevViewProj16);
        glUniformMatrix4fv(glGetUniformLocation(progTAA_, "uCurInvViewProj"),
                           1, GL_FALSE, curInvViewProj16);
        glUniform2f(glGetUniformLocation(progTAA_, "uRcpFrame"),
                    1.0f / float(width_), 1.0f / float(height_));
        glUniform1f(glGetUniformLocation(progTAA_, "uBlendNew"), taaBlendNew);
        glUniform1i(glGetUniformLocation(progTAA_, "uHistoryValid"),
                    taaHistoryValid_ ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ldrColor_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, taaHistory_[rIdx]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, hdrDepth_);
        glBindVertexArray(fsVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Pass 2: copy resolved history to default FB.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, taaFBO_);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width_, height_,
                          0, 0, screenW, screenH,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        taaWriteIndex_   = rIdx;          // next frame writes to the OTHER slot
        taaHistoryValid_ = true;
        glActiveTexture(GL_TEXTURE0);
    } else if (fxaaActive) {
        // --- FXAA: LDR intermediate -> default FB ---
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenW, screenH);
        glUseProgram(progFXAA_);
        glUniform1i(glGetUniformLocation(progFXAA_, "uScene"), 0);
        glUniform2f(glGetUniformLocation(progFXAA_, "uRcpFrame"),
                    1.0f / float(width_), 1.0f / float(height_));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ldrColor_);
        glBindVertexArray(fsVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // If neither AA path ran but TAA was on previously, invalidate history.
    if (!taaActive) {
        taaHistoryValid_ = false;
    }

    glBindVertexArray(0);
    glUseProgram(0);
    if (depthWas) glEnable(GL_DEPTH_TEST);

    // Consume per-frame flags so stale buffers don't bleed in.
    ssaoValidThisFrame_    = false;
    contactValidThisFrame_ = false;
    ssrValidThisFrame_     = false;
}

} // namespace ark
