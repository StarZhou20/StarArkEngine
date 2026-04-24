#version 450 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

const int MAX_DIR_LIGHTS   = 4;
const int MAX_POINT_LIGHTS = 8;
const int MAX_SPOT_LIGHTS  = 4;

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
    float innerCutoff;
    float outerCutoff;
};

uniform MaterialData uMaterial;
uniform vec3         uCameraPos;
uniform sampler2D    uDiffuseTex;

uniform int      uNumDirLights;
uniform DirLight uDirLights[MAX_DIR_LIGHTS];

uniform int        uNumPointLights;
uniform PointLight uPointLights[MAX_POINT_LIGHTS];

uniform int       uNumSpotLights;
uniform SpotLight uSpotLights[MAX_SPOT_LIGHTS];

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 matColor) {
    vec3 lightDir = normalize(-light.direction);
    vec3 ambient = light.ambient * matColor;
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * matColor;
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uMaterial.shininess);
    vec3 specular = light.color * spec * uMaterial.specular;
    return ambient + diffuse + specular;
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 viewDir, vec3 fragPos, vec3 matColor) {
    vec3 toLight = light.position - fragPos;
    float dist = length(toLight);
    if (dist > light.range) return vec3(0.0);
    vec3 lightDir = toLight / dist;
    float atten = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * matColor * atten;
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
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.innerCutoff - light.outerCutoff;
    float intensity = clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);
    if (intensity <= 0.0) return vec3(0.0);
    float atten = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.color * diff * matColor * atten * intensity;
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uMaterial.shininess);
    vec3 specular = light.color * spec * uMaterial.specular * atten * intensity;
    return diffuse + specular;
}

void main() {
    vec3 normal  = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 matColor = uMaterial.color.rgb;
    if (uMaterial.hasDiffuseTex != 0) {
        matColor = texture(uDiffuseTex, vTexCoord).rgb;
    }

    vec3 result = vec3(0.0);
    for (int i = 0; i < uNumDirLights; ++i) {
        result += CalcDirLight(uDirLights[i], normal, viewDir, matColor);
    }
    for (int i = 0; i < uNumPointLights; ++i) {
        result += CalcPointLight(uPointLights[i], normal, viewDir, vWorldPos, matColor);
    }
    for (int i = 0; i < uNumSpotLights; ++i) {
        result += CalcSpotLight(uSpotLights[i], normal, viewDir, vWorldPos, matColor);
    }

    if (uNumDirLights == 0 && uNumPointLights == 0 && uNumSpotLights == 0) {
        result = matColor * 0.3;
    }

    FragColor = vec4(result, uMaterial.color.a);
}
