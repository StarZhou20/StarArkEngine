#pragma once

namespace ark {

static const char* kPhongVS = R"(
#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uNormalMatrix) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

static const char* kPhongFS = R"(
#version 450 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

// ---------- limits ----------
const int MAX_DIR_LIGHTS   = 4;
const int MAX_POINT_LIGHTS = 8;
const int MAX_SPOT_LIGHTS  = 4;

// ---------- structs ----------
struct MaterialData {
    vec4  color;
    vec3  specular;
    float shininess;
    int   hasDiffuseTex;
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
    float innerCutoff;   // cos(inner angle)
    float outerCutoff;   // cos(outer angle)
};

// ---------- uniforms ----------
uniform MaterialData uMaterial;
uniform vec3         uCameraPos;
uniform sampler2D    uDiffuseTex;

uniform int      uNumDirLights;
uniform DirLight uDirLights[MAX_DIR_LIGHTS];

uniform int        uNumPointLights;
uniform PointLight uPointLights[MAX_POINT_LIGHTS];

uniform int       uNumSpotLights;
uniform SpotLight uSpotLights[MAX_SPOT_LIGHTS];

// ---------- helpers ----------
vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 matColor) {
    vec3 lightDir = normalize(-light.direction);

    // ambient
    vec3 ambient = light.ambient * matColor;

    // diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * matColor;

    // specular (Blinn-Phong)
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uMaterial.shininess);
    vec3 specular = light.color * spec * uMaterial.specular;

    return ambient + diffuse + specular;
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 viewDir, vec3 fragPos, vec3 matColor) {
    vec3 toLight = light.position - fragPos;
    float dist = length(toLight);

    // range culling
    if (dist > light.range) return vec3(0.0);

    vec3 lightDir = toLight / dist;

    // attenuation
    float atten = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);

    // diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * matColor * atten;

    // specular
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uMaterial.shininess);
    vec3 specular = light.color * spec * uMaterial.specular * atten;

    return diffuse + specular;
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 viewDir, vec3 fragPos, vec3 matColor) {
    vec3 toLight = light.position - fragPos;
    float dist = length(toLight);

    if (dist > light.range) return vec3(0.0);

    vec3 lightDir = toLight / dist;

    // spot cone
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.innerCutoff - light.outerCutoff;
    float intensity = clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);
    if (intensity <= 0.0) return vec3(0.0);

    // attenuation
    float atten = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);

    // diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * matColor * atten * intensity;

    // specular
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uMaterial.shininess);
    vec3 specular = light.color * spec * uMaterial.specular * atten * intensity;

    return diffuse + specular;
}

// ---------- main ----------
void main() {
    vec3 normal  = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 matColor = uMaterial.color.rgb;
    if (uMaterial.hasDiffuseTex != 0) {
        matColor = texture(uDiffuseTex, vTexCoord).rgb;
    }

    vec3 result = vec3(0.0);

    // directional lights
    for (int i = 0; i < uNumDirLights; ++i) {
        result += CalcDirLight(uDirLights[i], normal, viewDir, matColor);
    }

    // point lights
    for (int i = 0; i < uNumPointLights; ++i) {
        result += CalcPointLight(uPointLights[i], normal, viewDir, vWorldPos, matColor);
    }

    // spot lights
    for (int i = 0; i < uNumSpotLights; ++i) {
        result += CalcSpotLight(uSpotLights[i], normal, viewDir, vWorldPos, matColor);
    }

    // if no lights at all, use a basic fallback so objects are visible
    if (uNumDirLights == 0 && uNumPointLights == 0 && uNumSpotLights == 0) {
        result = matColor * 0.3;
    }

    FragColor = vec4(result, uMaterial.color.a);
}
)";

// ================================================================
// PBR (Cook-Torrance) shader — metallic-roughness workflow
// Same vertex shader as Phong, same multi-light uniform layout
// ================================================================

static const char* kPBR_VS = R"(
#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vTangent;
out vec3 vBitangent;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;

    mat3 normalMat = mat3(uNormalMatrix);
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent);
    // Re-orthogonalize tangent against normal (Gram-Schmidt), then build B.
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    vNormal    = N;
    vTangent   = T;
    vBitangent = B;

    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

static const char* kPBR_FS = R"(
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
// Keeps energy well-behaved while letting lights have a finite
// "range" for culling / artistic control.
float PhysicalAttenuation(float dist, float range) {
    float atten = 1.0 / max(dist * dist, 0.0001);
    // fade smoothly in the last 25% of the range
    float fade = 1.0 - smoothstep(range * 0.75, range, dist);
    return atten * fade;
}

vec3 CalcDirLightPBR(DirLight light, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 L = normalize(-light.direction);
    vec3 Lo = CookTorranceBRDF(L, V, N, albedo, metallic, roughness, light.color);
    vec3 ambient = light.ambient * albedo * uMaterial.ao;
    return ambient + Lo;
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
    // Interpolated TBN basis (may be non-orthonormal after interpolation).
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

    // Metallic / roughness: either from texture (glTF MR: G=roughness, B=metallic)
    // or scalar uniforms. Scalar values act as multipliers when a texture is bound.
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

    // directional lights
    for (int i = 0; i < uNumDirLights; ++i) {
        Lo += CalcDirLightPBR(uDirLights[i], N, V, albedo, metallic, roughness);
    }

    // point lights
    for (int i = 0; i < uNumPointLights; ++i) {
        Lo += CalcPointLightPBR(uPointLights[i], N, V, vWorldPos, albedo, metallic, roughness);
    }

    // spot lights
    for (int i = 0; i < uNumSpotLights; ++i) {
        Lo += CalcSpotLightPBR(uSpotLights[i], N, V, vWorldPos, albedo, metallic, roughness);
    }

    // Apply AO to the full direct-light accumulation as a cheap approximation
    // (true AO only affects ambient/indirect; will be refined once IBL lands).
    Lo *= ao;

    // fallback ambient if no lights
    if (uNumDirLights == 0 && uNumPointLights == 0 && uNumSpotLights == 0) {
        Lo = vec3(0.03) * albedo * ao;
    }

    // Emissive: scalar + texture (sRGB → linear on sample).
    vec3 emissive = uMaterial.emissive;
    if (uMaterial.hasEmissiveTex != 0) {
        emissive += texture(uEmissiveTex, vTexCoord).rgb;
    }
    Lo += emissive;

    // Output linear HDR radiance. Exposure and ACES tone mapping are applied
    // by the post-process composite pass (see PostProcess.cpp).
    FragColor = vec4(Lo, uMaterial.color.a);
}
)";

// ===========================================================================
// Depth-only pass (Phase 13 shadow maps). Only position attribute is read.
// ===========================================================================

static const char* kDepth_VS = R"(
#version 450 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;
void main() {
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPosition, 1.0);
}
)";

static const char* kDepth_FS = R"(
#version 450 core
void main() {}
)";

} // namespace ark
