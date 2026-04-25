#version 450 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vTangent;
in vec3 vBitangent;

out vec4 FragColor;

const float PI = 3.14159265359;

// ---------- limits ----------
const int MAX_DIR_LIGHTS   = 4;
const int MAX_POINT_LIGHTS = 8;
const int MAX_SPOT_LIGHTS  = 4;

// ---------- structs ----------
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

struct DirLight {
    vec3 direction;
    vec3 color;
    vec3 ambient;
};

struct PointLight {
    vec3  position;
    vec3  color;
    float constant;
    float linear;
    float quadratic;
    float range;
};

struct SpotLight {
    vec3  position;
    vec3  direction;
    vec3  color;
    float constant;
    float linear;
    float quadratic;
    float range;
    float innerCutoff;
    float outerCutoff;
};

// ---------- uniforms ----------
uniform MaterialData uMaterial;
uniform vec3         uCameraPos;
uniform float        uExposure;
uniform sampler2D    uDiffuseTex;
uniform sampler2D    uNormalTex;
uniform sampler2D    uMetalRoughTex;
uniform sampler2D    uAOTex;
uniform sampler2D    uEmissiveTex;

uniform int      uNumDirLights;
uniform DirLight uDirLights[MAX_DIR_LIGHTS];

uniform int        uNumPointLights;
uniform PointLight uPointLights[MAX_POINT_LIGHTS];

uniform int       uNumSpotLights;
uniform SpotLight uSpotLights[MAX_SPOT_LIGHTS];
// ---------- IBL (Phase 12) ----------
uniform int         uIBLEnabled;
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilterMap;
uniform sampler2D   uBrdfLUT;
uniform float       uIBLDiffuseIntensity;
uniform float       uIBLSpecularIntensity;
uniform float       uPrefilterMaxLod;

// ---------- Shadow (Phase 13, directional light [0] only) ----------
uniform int       uShadowEnabled;
uniform sampler2DShadow uShadowMap;
uniform mat4      uLightSpaceMatrix;
uniform float     uShadowDepthBias;
uniform float     uShadowNormalBias;
uniform int       uShadowPcfKernel;
uniform float     uShadowTexelSize;

// ---------- Debug visualisation (engine-set) ----------
// 0=off  1=albedo  2=world-normal  3=geom-normal  4=metallic  5=roughness
// 6=ao  7=mr-raw  8=uv  9=tangent
uniform int uDebugMode;
// ---------- PBR functions ----------

// Normal Distribution Function: GGX/Trowbridge-Reitz
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

// Geometry Function: Schlick-GGX (single direction)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Geometry Function: Smith's method (both view + light)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Fresnel: Schlick approximation
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance BRDF for a single radiance contribution
vec3 CookTorranceBRDF(vec3 L, vec3 V, vec3 N, vec3 albedo, float metallic, float roughness, vec3 radiance) {
    vec3 H = normalize(V + L);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular (Cook-Torrance)
    vec3 numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    // Energy conservation: kS + kD = 1
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// ---------- per-light-type helpers ----------

// Physically-based point-light falloff:
//   1 / (dist^2) with a smooth cutoff to zero at light.range.
float PhysicalAttenuation(float dist, float range) {
    float atten = 1.0 / max(dist * dist, 0.0001);
    float fade = 1.0 - smoothstep(range * 0.75, range, dist);
    return atten * fade;
}

vec3 CalcDirLightPBR(DirLight light, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 L = normalize(-light.direction);
    vec3 Lo = CookTorranceBRDF(L, V, N, albedo, metallic, roughness, light.color);
    vec3 ambient = light.ambient * albedo * uMaterial.ao;
    return ambient + Lo;
}

// PCF shadow sample for the primary directional light. Returns shadow
// coefficient in [0,1] (0 = fully lit, 1 = fully in shadow).
// Implementation: hardware 2x2 PCF (sampler2DShadow) + Vogel-disk
// distribution of N taps with per-pixel rotation. Disk-based sampling
// has no aligned axis so the residual aliasing has no preferred
// direction and reads as soft-noise instead of grid-stepped tiers.
float InterleavedGradientNoise(vec2 p) {
    // Jorge Jimenez 2014.
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

float SampleDirShadow(vec3 worldPos, vec3 N, vec3 L) {
    // Back-facing to the light = fully shadowed.
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return 1.0;

    // World-space normal bias. Cap the (1-NdotL) factor so grazing pixels
    // at the seam between two meshes don't get pushed an outsized distance
    // (which is the cause of "light leaking through cracks").
    float slope = clamp(1.0 - NdotL, 0.0, 0.5) * 2.0;  // 0..1 but rises slower
    vec3  biasedPos = worldPos + N * (uShadowNormalBias * slope);

    vec4 lightClip = uLightSpaceMatrix * vec4(biasedPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    // Smooth fade near the edges of the shadow map to hide the hard
    // boundary as the camera moves. Compute distance to the [0,1] frame
    // and fade shadow toward 0 in the outer ~10% margin.
    vec2 fadeUV = min(proj.xy, 1.0 - proj.xy);     // 0 at edge, 0.5 at center
    float edgeFade = smoothstep(0.0, 0.10, min(fadeUV.x, fadeUV.y));
    // Same idea for far depth: fade out as proj.z approaches 1.
    float zFade   = 1.0 - smoothstep(0.85, 1.0, proj.z);
    float fade    = edgeFade * zFade;
    if (fade <= 0.0) return 0.0;

    float currentDepth = proj.z - uShadowDepthBias;

    // Filter radius: scale shadow texel by (kernel + 1). Tight radius keeps
    // noise low; relying on hw 2x2 PCF + 32 taps for softness.
    float radiusTex = float(uShadowPcfKernel) + 0.5;
    vec2  filterR   = vec2(uShadowTexelSize) * radiusTex;

    // 4-rotation dither anchored to 2x2 pixel blocks.
    // Per-pixel rotation flickers when the camera moves because the noise
    // is screen-anchored (same world point lands on different fragCoord ->
    // different rotation -> different shadow value). Using only 4 distinct
    // angles snapped to a 2x2 block gives enough variation to break the
    // tap grid alignment, but keeps the rotation stable across small camera
    // motions (a world point's screen pos has to move > 2 pixels before its
    // rotation changes).
    ivec2 cell = ivec2(gl_FragCoord.xy) >> 1;
    float idx  = float((cell.x ^ cell.y) & 3);              // 0..3
    float ang  = idx * 1.57079632679;                       // pi/2 steps
    float cs = cos(ang);
    float sn = sin(ang);

    // 16-tap Vogel disk (golden angle).
    const int   N_TAPS = 16;
    const float GOLDEN = 2.39996323;
    float shadow = 0.0;
    for (int i = 0; i < N_TAPS; ++i) {
        float r = sqrt((float(i) + 0.5) / float(N_TAPS));
        float a = float(i) * GOLDEN;
        vec2 off = vec2(cos(a), sin(a)) * r;
        // Rotate.
        off = vec2(off.x * cs - off.y * sn, off.x * sn + off.y * cs);
        off *= filterR;
        // sampler2DShadow returns 1.0 if depth_ref <= stored, i.e. lit.
        shadow += 1.0 - texture(uShadowMap, vec3(proj.xy + off, currentDepth));
    }
    return (shadow / float(N_TAPS)) * fade;
}

vec3 CalcDirLightPBRShadowed(DirLight light, vec3 N, vec3 V, vec3 albedo,
                             float metallic, float roughness, float shadow) {
    vec3 L = normalize(-light.direction);
    vec3 Lo = CookTorranceBRDF(L, V, N, albedo, metallic, roughness, light.color);
    vec3 ambient = light.ambient * albedo * uMaterial.ao;
    return ambient + (1.0 - shadow) * Lo;
}

vec3 CalcPointLightPBR(PointLight light, vec3 N, vec3 V, vec3 fragPos, vec3 albedo, float metallic, float roughness) {
    vec3 toLight = light.position - fragPos;
    float dist = length(toLight);
    if (dist > light.range) return vec3(0.0);

    vec3 L = toLight / dist;
    vec3 radiance = light.color * PhysicalAttenuation(dist, light.range);

    return CookTorranceBRDF(L, V, N, albedo, metallic, roughness, radiance);
}

vec3 CalcSpotLightPBR(SpotLight light, vec3 N, vec3 V, vec3 fragPos, vec3 albedo, float metallic, float roughness) {
    vec3 toLight = light.position - fragPos;
    float dist = length(toLight);
    if (dist > light.range) return vec3(0.0);

    vec3 L = toLight / dist;

    float theta = dot(L, normalize(-light.direction));
    float epsilon = light.innerCutoff - light.outerCutoff;
    float spotIntensity = clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);
    if (spotIntensity <= 0.0) return vec3(0.0);

    vec3 radiance = light.color * PhysicalAttenuation(dist, light.range) * spotIntensity;

    return CookTorranceBRDF(L, V, N, albedo, metallic, roughness, radiance);
}

// ---------- main ----------
void main() {
    vec3 N_geom = normalize(vNormal);

    // Sample albedo (sRGB texture → already linear on sample).
    vec3 albedo = uMaterial.color.rgb;
    float alpha = uMaterial.color.a;
    if (uMaterial.hasDiffuseTex != 0) {
        vec4 diff = texture(uDiffuseTex, vTexCoord);
        albedo *= diff.rgb;
        alpha  *= diff.a;
    }
    // Alpha cutout for foliage / decals (avoids black fringes around leaves).
    if (alpha < 0.5) discard;

    // Resolve surface normal: either from normal map (tangent-space) or geometry.
    vec3 N;
    if (uMaterial.hasNormalTex != 0) {
        vec3 T = normalize(vTangent);
        vec3 B = normalize(vBitangent);
        mat3 TBN = mat3(T, B, N_geom);

        // Reconstruct Z from XY so this works for both BC5 (RG-only) and BC7
        // normal maps. Bistro ships BC5 for foliage; reading raw .z gives a
        // constant that flips the tangent-space normal.
        vec2 nxy = texture(uNormalTex, vTexCoord).xy * 2.0 - 1.0;
        float nz = sqrt(max(0.0, 1.0 - dot(nxy, nxy)));
        vec3 tangentNormal = vec3(nxy, nz);
        N = normalize(TBN * tangentNormal);
    } else {
        N = N_geom;
    }

    vec3 V = normalize(uCameraPos - vWorldPos);

    // Metallic / roughness: when the MR texture is bound, use its channels
    // directly (not as a multiplier). The scalar defaults are fallbacks only,
    // so asset-authored values aren't accidentally zeroed out.
    float metallic;
    float roughness;
    if (uMaterial.hasMetalRoughTex != 0) {
        vec3 mr = texture(uMetalRoughTex, vTexCoord).rgb;
        roughness = mr.g;
        // Bistro's Specular texture often leaves B=0 (no metallic packed).
        // Fall back to the material scalar so gold/metal stays configurable.
        metallic = (mr.b < 0.01) ? uMaterial.metallic : mr.b;
    } else {
        metallic  = uMaterial.metallic;
        roughness = uMaterial.roughness;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic, 0.0, 1.0);

    // AO: direct texture sample when bound, else scalar fallback.
    // When the AO map is actually the Specular texture's R channel (Bistro
    // pattern), R may be 0 meaning "no AO packed" — treat that as fully lit
    // rather than letting it kill all ambient/IBL light.
    float ao;
    if (uMaterial.hasAOTex != 0) {
        float aoSample = texture(uAOTex, vTexCoord).r;
        ao = (aoSample < 0.01) ? uMaterial.ao : aoSample;
    } else {
        ao = uMaterial.ao;
    }

    vec3 Lo = vec3(0.0);

    float mainShadow = 0.0;  // shared with IBL block to dim sky-bounce in caves
    for (int i = 0; i < uNumDirLights; ++i) {
        float shadow = 0.0;
        if (i == 0 && uShadowEnabled != 0) {
            vec3 L = normalize(-uDirLights[i].direction);
            shadow = SampleDirShadow(vWorldPos, N, L);
            mainShadow = shadow;
        }
        Lo += CalcDirLightPBRShadowed(uDirLights[i], N, V, albedo, metallic, roughness, shadow);
    }
    for (int i = 0; i < uNumPointLights; ++i) {
        Lo += CalcPointLightPBR(uPointLights[i], N, V, vWorldPos, albedo, metallic, roughness);
    }
    for (int i = 0; i < uNumSpotLights; ++i) {
        Lo += CalcSpotLightPBR(uSpotLights[i], N, V, vWorldPos, albedo, metallic, roughness);
    }

    // NOTE: AO is applied inside each Calc*LightPBR's ambient term, and to the
    // IBL diffuse/specular below. Do NOT multiply the direct-light accumulation
    // by AO — doing so makes the scene nearly black when AO textures are dark
    // (common in outdoor/Bistro-style PBR assets).

    // --- IBL ambient (Phase 12) ---
    if (uIBLEnabled != 0) {
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F  = F0 + (max(vec3(1.0 - roughness), F0) - F0) *
                 pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 5.0);
        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metallic);

        vec3 irradiance = texture(uIrradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo * uIBLDiffuseIntensity;

        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * uPrefilterMaxLod).rgb;
        vec2 envBRDF = texture(uBrdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
        vec3 specularIBL = prefiltered * (F * envBRDF.x + envBRDF.y) * uIBLSpecularIntensity;

        // Specular IBL is "sky reflection". In areas the sun can't reach
        // (caves, archways, recesses) it should be attenuated; otherwise
        // dark interiors get a bright sky-mirror leak. Multiply by
        // (1 - mainShadow * 0.85) -> shadowed regions keep ~15% specular
        // (still some bounce light) but lose the strong sky highlight.
        specularIBL *= (1.0 - mainShadow * 0.85);

        Lo += (kD * diffuseIBL + specularIBL) * ao;
    } else if (uNumDirLights == 0 && uNumPointLights == 0 && uNumSpotLights == 0) {
        Lo = vec3(0.03) * albedo * ao;
    }

    vec3 emissive = uMaterial.emissive;
    if (uMaterial.hasEmissiveTex != 0) {
        emissive += texture(uEmissiveTex, vTexCoord).rgb;
    }
    Lo += emissive;

    // ---- Debug visualisation (overrides final color) ----
    if (uDebugMode != 0) {
        vec3 dbg = vec3(0.0);
        if      (uDebugMode == 1) dbg = albedo;
        else if (uDebugMode == 2) dbg = N * 0.5 + 0.5;
        else if (uDebugMode == 3) dbg = N_geom * 0.5 + 0.5;
        else if (uDebugMode == 4) dbg = vec3(metallic);
        else if (uDebugMode == 5) dbg = vec3(roughness);
        else if (uDebugMode == 6) dbg = vec3(ao);
        else if (uDebugMode == 7) dbg = (uMaterial.hasMetalRoughTex != 0)
                                        ? texture(uMetalRoughTex, vTexCoord).rgb
                                        : vec3(1.0, 0.0, 1.0);
        else if (uDebugMode == 8) dbg = vec3(vTexCoord, 0.0);
        else if (uDebugMode == 9) dbg = normalize(vTangent) * 0.5 + 0.5;
        else if (uDebugMode == 10) {
            // Shadow factor: white = lit, black = in shadow.
            float s = 0.0;
            if (uShadowEnabled != 0 && uNumDirLights > 0) {
                vec3 L = normalize(-uDirLights[0].direction);
                s = SampleDirShadow(vWorldPos, N, L);
            }
            dbg = vec3(1.0 - s);
        }
        FragColor = vec4(dbg, 1.0);
        return;
    }

    // Output linear HDR radiance. Exposure and ACES tone mapping are
    // applied by the post-process composite pass (see PostProcess.cpp).
    FragColor = vec4(Lo, alpha);
}
