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
uniform sampler2D uShadowMap;
uniform mat4      uLightSpaceMatrix;
uniform float     uShadowDepthBias;
uniform float     uShadowNormalBias;
uniform int       uShadowPcfKernel;
uniform float     uShadowTexelSize;
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
float SampleDirShadow(vec3 worldPos, vec3 N, vec3 L) {
    vec4 lightClip = uLightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float NdotL = max(dot(N, L), 0.0);
    float bias  = max(uShadowNormalBias * (1.0 - NdotL), uShadowDepthBias);
    float currentDepth = proj.z - bias;

    int k = uShadowPcfKernel;
    float shadow = 0.0;
    float samples = 0.0;
    for (int x = -k; x <= k; ++x) {
        for (int y = -k; y <= k; ++y) {
            vec2 off = vec2(x, y) * uShadowTexelSize;
            float closest = texture(uShadowMap, proj.xy + off).r;
            shadow += (currentDepth > closest) ? 1.0 : 0.0;
            samples += 1.0;
        }
    }
    return shadow / max(samples, 1.0);
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
    if (uMaterial.hasDiffuseTex != 0) {
        albedo *= texture(uDiffuseTex, vTexCoord).rgb;
    }

    // Resolve surface normal: either from normal map (tangent-space) or geometry.
    vec3 N;
    if (uMaterial.hasNormalTex != 0) {
        vec3 T = normalize(vTangent);
        vec3 B = normalize(vBitangent);
        mat3 TBN = mat3(T, B, N_geom);

        vec3 tangentNormal = texture(uNormalTex, vTexCoord).xyz * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    } else {
        N = N_geom;
    }

    vec3 V = normalize(uCameraPos - vWorldPos);

    // Metallic / roughness: texture (glTF MR: G=roughness, B=metallic) * scalar.
    float metallic  = uMaterial.metallic;
    float roughness = uMaterial.roughness;
    if (uMaterial.hasMetalRoughTex != 0) {
        vec3 mr = texture(uMetalRoughTex, vTexCoord).rgb;
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic, 0.0, 1.0);

    // AO: scalar * texture (R channel, linear).
    float ao = uMaterial.ao;
    if (uMaterial.hasAOTex != 0) {
        ao *= texture(uAOTex, vTexCoord).r;
    }

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < uNumDirLights; ++i) {
        float shadow = 0.0;
        if (i == 0 && uShadowEnabled != 0) {
            vec3 L = normalize(-uDirLights[i].direction);
            shadow = SampleDirShadow(vWorldPos, N, L);
        }
        Lo += CalcDirLightPBRShadowed(uDirLights[i], N, V, albedo, metallic, roughness, shadow);
    }
    for (int i = 0; i < uNumPointLights; ++i) {
        Lo += CalcPointLightPBR(uPointLights[i], N, V, vWorldPos, albedo, metallic, roughness);
    }
    for (int i = 0; i < uNumSpotLights; ++i) {
        Lo += CalcSpotLightPBR(uSpotLights[i], N, V, vWorldPos, albedo, metallic, roughness);
    }

    Lo *= ao;

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

        Lo += (kD * diffuseIBL + specularIBL) * ao;
    } else if (uNumDirLights == 0 && uNumPointLights == 0 && uNumSpotLights == 0) {
        Lo = vec3(0.03) * albedo * ao;
    }

    vec3 emissive = uMaterial.emissive;
    if (uMaterial.hasEmissiveTex != 0) {
        emissive += texture(uEmissiveTex, vTexCoord).rgb;
    }
    Lo += emissive;

    // Output linear HDR radiance. Exposure and ACES tone mapping are
    // applied by the post-process composite pass (see PostProcess.cpp).
    FragColor = vec4(Lo, uMaterial.color.a);
}
