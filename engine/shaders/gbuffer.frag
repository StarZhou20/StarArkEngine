#version 450 core
// G-buffer geometry pass fragment shader (Roadmap #9 Deferred).
//
// Writes the surface attributes needed by a fullscreen deferred lighting
// pass. The lighting pass reconstructs worldPos from the depth buffer
// (D32F texture sampled alongside the colour attachments), so we do NOT
// need a worldPos colour attachment — saves 16 bytes/pixel.
//
// G-buffer layout (must match DeferredRenderer::AllocateTargets):
//   RT0  RGBA8_UNorm   .rgb = albedo (linear, post-alpha-cutout)
//                      .a   = metallic
//   RT1  RGBA16F       .rgb = world-space normal (signed, ~unit length)
//                      .a   = roughness
//   RT2  RGBA16F       .rgb = emissive (HDR linear)
//                      .a   = 0  (motion vector scratch — TBD)
//   RT3  RGBA8_UNorm   .r   = ao
//                      .gba = 0  (flags / material id — TBD)
//
// Material uniform block matches pbr.frag so a single Material can feed
// both forward and deferred paths via MaterialPass::{Forward,GBuffer}.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vTangent;
in vec3 vBitangent;

layout(location = 0) out vec4 oAlbedoMetallic;
layout(location = 1) out vec4 oNormalRoughness;
layout(location = 2) out vec4 oEmissiveMotion;
layout(location = 3) out vec4 oAOFlags;

struct MaterialData {
    vec4  color;
    vec3  specular;
    float shininess;
    float metallic;
    float roughness;
    float ao;
    vec3  emissive;
    int   hasDiffuseTex;
    int   hasNormalTex;
    int   hasMetalRoughTex;
    int   hasAOTex;
    int   hasEmissiveTex;
};

uniform MaterialData uMaterial;
uniform sampler2D    uDiffuseTex;
uniform sampler2D    uNormalTex;
uniform sampler2D    uMetalRoughTex;
uniform sampler2D    uAOTex;
uniform sampler2D    uEmissiveTex;

void main() {
    vec3 N_geom = normalize(vNormal);

    // --- Albedo + alpha cutout (matches pbr.frag) ---
    vec3 albedo = uMaterial.color.rgb;
    float alpha = uMaterial.color.a;
    if (uMaterial.hasDiffuseTex != 0) {
        vec4 d = texture(uDiffuseTex, vTexCoord);
        albedo *= d.rgb;
        alpha  *= d.a;
    }
    if (alpha < 0.5) discard;

    // --- Surface normal (TBN + reconstructed Z, same as pbr.frag) ---
    vec3 N;
    if (uMaterial.hasNormalTex != 0) {
        vec3 T = normalize(vTangent);
        vec3 B = normalize(vBitangent);
        mat3 TBN = mat3(T, B, N_geom);
        vec2 nxy = texture(uNormalTex, vTexCoord).xy * 2.0 - 1.0;
        float nz = sqrt(max(0.0, 1.0 - dot(nxy, nxy)));
        N = normalize(TBN * vec3(nxy, nz));
    } else {
        N = N_geom;
    }

    // --- Metallic / roughness ---
    float metallic;
    float roughness;
    if (uMaterial.hasMetalRoughTex != 0) {
        vec3 mr = texture(uMetalRoughTex, vTexCoord).rgb;
        roughness = mr.g;
        metallic  = (mr.b < 0.01) ? uMaterial.metallic : mr.b;
    } else {
        metallic  = uMaterial.metallic;
        roughness = uMaterial.roughness;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic,  0.0,  1.0);

    // --- AO ---
    float ao;
    if (uMaterial.hasAOTex != 0) {
        float a = texture(uAOTex, vTexCoord).r;
        ao = (a < 0.01) ? uMaterial.ao : a;
    } else {
        ao = uMaterial.ao;
    }

    // --- Emissive ---
    vec3 emissive = uMaterial.emissive;
    if (uMaterial.hasEmissiveTex != 0) {
        emissive += texture(uEmissiveTex, vTexCoord).rgb;
    }

    // --- MRT writes ---
    oAlbedoMetallic  = vec4(albedo, metallic);
    oNormalRoughness = vec4(N, roughness);
    oEmissiveMotion  = vec4(emissive, 0.0);
    oAOFlags         = vec4(ao, 0.0, 0.0, 0.0);
}
