#version 450 core
// Deferred lighting fragment shader (Roadmap #9 stage C).
//
// Inputs: 4-RT G-buffer + Depth32F texture (sampled for worldPos
// reconstruction). Output: HDR linear radiance pre-tone-mapped to LDR
// sRGB so a plain blit can present it.
//
// Stage C scope: directional + point + spot lights (no shadow map, no
// IBL, no fog). Stage D plugs shadow + IBL.

in vec2 vUV;
out vec4 FragColor;

const float PI = 3.14159265359;

// G-buffer (units 0..3) + depth (4)
uniform sampler2D uGAlbedoMetallic;   // RT0  .rgb=albedo  .a=metallic
uniform sampler2D uGNormalRoughness;  // RT1  .xyz=normal  .a=roughness
uniform sampler2D uGEmissiveMotion;   // RT2  .rgb=emissive
uniform sampler2D uGAOFlags;          // RT3  .r=ao
uniform sampler2D uGDepth;            // D32F

// Camera + transforms for worldPos reconstruction
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform float uExposure;
// When 0, write linear HDR (for downstream PostProcess); when 1, apply
// inline ACES + 1/2.2 gamma so a plain blit can present lit_.
uniform int  uApplyToneMap;

// IBL (Phase 12 — Stage D in deferred). Units 5/6/7.
uniform int         uIBLEnabled;
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilterMap;
uniform sampler2D   uBrdfLUT;
uniform float       uIBLDiffuseIntensity;
uniform float       uIBLSpecularIntensity;
uniform float       uPrefilterMaxLod;

// Directional shadow map (Phase 13 — Stage D in deferred). Unit 8.
uniform int             uShadowEnabled;
uniform sampler2DShadow uShadowMap;
uniform mat4            uLightSpaceMatrix;
uniform float           uShadowDepthBias;
uniform float           uShadowNormalBias;
uniform int             uShadowPcfKernel;
uniform float           uShadowTexelSize;

// ---------- limits ----------
const int MAX_DIR_LIGHTS   = 4;
const int MAX_POINT_LIGHTS = 8;
const int MAX_SPOT_LIGHTS  = 4;

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

uniform int      uNumDirLights;
uniform DirLight uDirLights[MAX_DIR_LIGHTS];

uniform int        uNumPointLights;
uniform PointLight uPointLights[MAX_POINT_LIGHTS];

uniform int       uNumSpotLights;
uniform SpotLight uSpotLights[MAX_SPOT_LIGHTS];

// ---------- PBR (mirrors pbr.frag exactly) ----------
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 CookTorranceBRDF(vec3 L, vec3 V, vec3 N, vec3 albedo,
                      float metallic, float roughness, vec3 radiance) {
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

float PhysicalAttenuation(float dist, float range) {
    float atten = 1.0 / max(dist * dist, 0.0001);
    float fade = 1.0 - smoothstep(range * 0.75, range, dist);
    return atten * fade;
}

// PCF shadow sample (mirrors pbr.frag SampleDirShadow). Returns 1 = shadow,
// 0 = lit. Only sampled for the primary directional light (i==0).
float SampleDirShadow(vec3 worldPos, vec3 N, vec3 L) {
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return 1.0;
    float slope = clamp(1.0 - NdotL, 0.0, 0.5) * 2.0;
    vec3  biasedPos = worldPos + N * (uShadowNormalBias * slope);

    vec4 lightClip = uLightSpaceMatrix * vec4(biasedPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    vec2 fadeUV = min(proj.xy, 1.0 - proj.xy);
    float edgeFade = smoothstep(0.0, 0.10, min(fadeUV.x, fadeUV.y));
    float zFade   = 1.0 - smoothstep(0.85, 1.0, proj.z);
    float fade    = edgeFade * zFade;
    if (fade <= 0.0) return 0.0;

    float currentDepth = proj.z - uShadowDepthBias;
    float radiusTex = float(uShadowPcfKernel) + 0.5;
    vec2  filterR   = vec2(uShadowTexelSize) * radiusTex;

    ivec2 cell = ivec2(gl_FragCoord.xy) >> 1;
    float idx  = float((cell.x ^ cell.y) & 3);
    float ang  = idx * 1.57079632679;
    float cs = cos(ang);
    float sn = sin(ang);

    const int   N_TAPS = 16;
    const float GOLDEN = 2.39996323;
    float shadow = 0.0;
    for (int i = 0; i < N_TAPS; ++i) {
        float r = sqrt((float(i) + 0.5) / float(N_TAPS));
        float a = float(i) * GOLDEN;
        vec2 off = vec2(cos(a), sin(a)) * r;
        off = vec2(off.x * cs - off.y * sn, off.x * sn + off.y * cs);
        off *= filterR;
        shadow += 1.0 - texture(uShadowMap, vec3(proj.xy + off, currentDepth));
    }
    return (shadow / float(N_TAPS)) * fade;
}

// ACES Fitted (Krzysztof Narkowicz approximation)
vec3 ACESFitted(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // --- Sample G-buffer ----
    vec4 albedoMetallic  = texture(uGAlbedoMetallic,  vUV);
    vec4 normalRoughness = texture(uGNormalRoughness, vUV);
    vec3 emissive        = texture(uGEmissiveMotion,  vUV).rgb;
    float ao             = texture(uGAOFlags,         vUV).r;
    float depth          = texture(uGDepth,           vUV).r;

    // Skip pixels with no geometry written (cleared depth = 1.0 OR
    // alpha=0 marker from G-buffer clear). Output black; lighting=0.
    if (depth >= 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3  albedo    = albedoMetallic.rgb;
    float metallic  = clamp(albedoMetallic.a, 0.0, 1.0);
    vec3  N         = normalize(normalRoughness.xyz);
    float roughness = clamp(normalRoughness.a, 0.04, 1.0);

    // --- Reconstruct worldPos from depth ---
    vec3 ndc = vec3(vUV * 2.0 - 1.0, depth * 2.0 - 1.0);
    vec4 wp4 = uInvViewProj * vec4(ndc, 1.0);
    vec3 worldPos = wp4.xyz / wp4.w;

    vec3 V = normalize(uCameraPos - worldPos);

    // --- Direct light accumulation ---
    vec3 Lo = vec3(0.0);
    float mainShadow = 0.0;  // shared with IBL block to dim sky-bounce in caves

    for (int i = 0; i < uNumDirLights; ++i) {
        vec3 L = normalize(-uDirLights[i].direction);
        float shadow = 0.0;
        if (i == 0 && uShadowEnabled != 0) {
            shadow = SampleDirShadow(worldPos, N, L);
            mainShadow = shadow;
        }
        vec3 direct = CookTorranceBRDF(L, V, N, albedo, metallic, roughness, uDirLights[i].color);
        Lo += uDirLights[i].ambient * albedo * ao + (1.0 - shadow) * direct;
    }
    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toL = uPointLights[i].position - worldPos;
        float dist = length(toL);
        if (dist > uPointLights[i].range) continue;
        vec3 L = toL / dist;
        vec3 radiance = uPointLights[i].color *
                        PhysicalAttenuation(dist, uPointLights[i].range);
        Lo += CookTorranceBRDF(L, V, N, albedo, metallic, roughness, radiance);
    }
    for (int i = 0; i < uNumSpotLights; ++i) {
        vec3 toL = uSpotLights[i].position - worldPos;
        float dist = length(toL);
        if (dist > uSpotLights[i].range) continue;
        vec3 L = toL / dist;
        float theta = dot(L, normalize(-uSpotLights[i].direction));
        float epsilon = uSpotLights[i].innerCutoff - uSpotLights[i].outerCutoff;
        float spotIntensity = clamp(
            (theta - uSpotLights[i].outerCutoff) / epsilon, 0.0, 1.0);
        if (spotIntensity <= 0.0) continue;
        vec3 radiance = uSpotLights[i].color *
                        PhysicalAttenuation(dist, uSpotLights[i].range) *
                        spotIntensity;
        Lo += CookTorranceBRDF(L, V, N, albedo, metallic, roughness, radiance);
    }

    // --- IBL ambient (Phase 12, Stage D) ---
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

        // Dim sky-bounce specular in shadowed regions (cave/archway leak fix
        // — same heuristic as pbr.frag).
        specularIBL *= (1.0 - mainShadow * 0.85);

        Lo += (kD * diffuseIBL + specularIBL) * ao;
    } else if (uNumDirLights == 0 && uNumPointLights == 0 && uNumSpotLights == 0) {
        // No-light fallback so unlit scenes aren't fully black.
        Lo = vec3(0.03) * albedo * ao;
    }

    Lo += emissive;

    // --- Output: linear HDR or inline-toned LDR depending on uApplyToneMap ---
    if (uApplyToneMap != 0) {
        vec3 mapped = ACESFitted(Lo * uExposure);
        vec3 srgb   = pow(mapped, vec3(1.0 / 2.2));
        FragColor = vec4(srgb, 1.0);
    } else {
        FragColor = vec4(Lo * uExposure, 1.0);
    }
}
