#include "SceneSerializer.h"

#include "engine/rendering/ForwardRenderer.h"
#include "engine/rendering/Light.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"
#include "engine/debug/DebugListenBus.h"

#include <glm/gtc/quaternion.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ark {

// ---------------------------------------------------------------------------
// Minimal JSON value + hand-written parser. Not a general-purpose JSON impl;
// intentionally restricted to the flat schema produced by Save() below.
// ---------------------------------------------------------------------------
namespace {

struct JValue;
using JObject = std::unordered_map<std::string, JValue>;
using JArray  = std::vector<JValue>;

struct JValue {
    // Order matters for index() comparisons below.
    std::variant<std::monostate, bool, double, std::string, JArray, JObject> v;

    bool   IsObject() const { return v.index() == 5; }
    bool   IsArray()  const { return v.index() == 4; }
    bool   IsNumber() const { return v.index() == 2; }
    bool   IsBool()   const { return v.index() == 1; }
    bool   IsString() const { return v.index() == 3; }

    const JObject& AsObject() const { return std::get<JObject>(v); }
    const JArray&  AsArray()  const { return std::get<JArray>(v); }
    double         AsNumber() const { return std::get<double>(v); }
    bool           AsBool()   const { return std::get<bool>(v); }
    const std::string& AsString() const { return std::get<std::string>(v); }
};

// --- Parser ---------------------------------------------------------------

class Parser {
public:
    explicit Parser(const std::string& src) : src_(src) {}
    std::optional<JValue> ParseRoot() {
        SkipWs();
        auto v = ParseValue();
        if (!v) return std::nullopt;
        SkipWs();
        return v;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;

    void SkipWs() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }
    bool Peek(char c) { SkipWs(); return pos_ < src_.size() && src_[pos_] == c; }
    bool Eat(char c)  { if (Peek(c)) { ++pos_; return true; } return false; }

    std::optional<JValue> ParseValue() {
        SkipWs();
        if (pos_ >= src_.size()) return std::nullopt;
        char c = src_[pos_];
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == 't' || c == 'f') return ParseBool();
        if (c == 'n') { // null
            if (src_.compare(pos_, 4, "null") == 0) { pos_ += 4; return JValue{}; }
            return std::nullopt;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return ParseNumber();
        return std::nullopt;
    }

    std::optional<JValue> ParseObject() {
        if (!Eat('{')) return std::nullopt;
        JObject out;
        SkipWs();
        if (Eat('}')) { JValue v; v.v = std::move(out); return v; }
        while (true) {
            SkipWs();
            auto keyVal = ParseString();
            if (!keyVal) return std::nullopt;
            std::string key = std::move(std::get<std::string>(keyVal->v));
            SkipWs();
            if (!Eat(':')) return std::nullopt;
            auto val = ParseValue();
            if (!val) return std::nullopt;
            out.emplace(std::move(key), std::move(*val));
            SkipWs();
            if (Eat(',')) continue;
            if (Eat('}')) break;
            return std::nullopt;
        }
        JValue v; v.v = std::move(out); return v;
    }

    std::optional<JValue> ParseArray() {
        if (!Eat('[')) return std::nullopt;
        JArray out;
        SkipWs();
        if (Eat(']')) { JValue v; v.v = std::move(out); return v; }
        while (true) {
            auto val = ParseValue();
            if (!val) return std::nullopt;
            out.push_back(std::move(*val));
            SkipWs();
            if (Eat(',')) continue;
            if (Eat(']')) break;
            return std::nullopt;
        }
        JValue v; v.v = std::move(out); return v;
    }

    std::optional<JValue> ParseString() {
        if (!Eat('"')) return std::nullopt;
        std::string out;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') { JValue v; v.v = std::move(out); return v; }
            if (c == '\\' && pos_ < src_.size()) {
                char e = src_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    default:  out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<JValue> ParseNumber() {
        size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        while (pos_ < src_.size() && (std::isdigit(static_cast<unsigned char>(src_[pos_])) ||
                                      src_[pos_] == '.' || src_[pos_] == 'e' ||
                                      src_[pos_] == 'E' || src_[pos_] == '+' ||
                                      src_[pos_] == '-')) ++pos_;
        std::string tok = src_.substr(start, pos_ - start);
        JValue v; v.v = std::atof(tok.c_str()); return v;
    }

    std::optional<JValue> ParseBool() {
        if (src_.compare(pos_, 4, "true") == 0)  { pos_ += 4; JValue v; v.v = true;  return v; }
        if (src_.compare(pos_, 5, "false") == 0) { pos_ += 5; JValue v; v.v = false; return v; }
        return std::nullopt;
    }
};

// --- Field helpers (tolerant of missing keys) ---

const JValue* Find(const JObject& o, const char* key) {
    auto it = o.find(key);
    return it == o.end() ? nullptr : &it->second;
}

float FloatOr(const JObject& o, const char* key, float def) {
    const JValue* v = Find(o, key);
    return (v && v->IsNumber()) ? static_cast<float>(v->AsNumber()) : def;
}
int IntOr(const JObject& o, const char* key, int def) {
    const JValue* v = Find(o, key);
    return (v && v->IsNumber()) ? static_cast<int>(v->AsNumber()) : def;
}
bool BoolOr(const JObject& o, const char* key, bool def) {
    const JValue* v = Find(o, key);
    return (v && v->IsBool()) ? v->AsBool() : def;
}
std::string StringOr(const JObject& o, const char* key, const char* def) {
    const JValue* v = Find(o, key);
    return (v && v->IsString()) ? v->AsString() : std::string(def);
}
glm::vec3 Vec3Or(const JObject& o, const char* key, const glm::vec3& def) {
    const JValue* v = Find(o, key);
    if (!v || !v->IsArray()) return def;
    const JArray& a = v->AsArray();
    if (a.size() < 3) return def;
    glm::vec3 out = def;
    if (a[0].IsNumber()) out.x = static_cast<float>(a[0].AsNumber());
    if (a[1].IsNumber()) out.y = static_cast<float>(a[1].AsNumber());
    if (a[2].IsNumber()) out.z = static_cast<float>(a[2].AsNumber());
    return out;
}

// --- Writer ---------------------------------------------------------------

struct Writer {
    std::ostringstream os;
    int indent = 0;
    void Indent() { for (int i = 0; i < indent; ++i) os << "  "; }

    void Str(const std::string& s) {
        os << '"';
        for (char c : s) {
            switch (c) {
                case '"':  os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\n': os << "\\n";  break;
                case '\t': os << "\\t";  break;
                default:   os << c;       break;
            }
        }
        os << '"';
    }
    void Float(float f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", f);
        os << buf;
    }
    void Vec3(const glm::vec3& v) {
        os << "[";
        Float(v.x); os << ", ";
        Float(v.y); os << ", ";
        Float(v.z); os << "]";
    }
};

std::string TypeToStr(Light::Type t) {
    switch (t) {
        case Light::Type::Directional: return "Directional";
        case Light::Type::Point:       return "Point";
        case Light::Type::Spot:        return "Spot";
    }
    return "Directional";
}
Light::Type StrToType(const std::string& s) {
    if (s == "Point")       return Light::Type::Point;
    if (s == "Spot")        return Light::Type::Spot;
    return Light::Type::Directional;
}

// Euler <-> quaternion. We use YXZ (yaw, pitch, roll) for readability.
glm::vec3 QuatToEuler(const glm::quat& q) {
    // Returns radians as (pitch=X, yaw=Y, roll=Z).
    return glm::eulerAngles(q);
}
glm::quat EulerToQuat(const glm::vec3& eulerDeg) {
    glm::vec3 r = glm::radians(eulerDeg);
    return glm::quat(r);
}

} // namespace

// ---------------------------------------------------------------------------
// Save / Load
// ---------------------------------------------------------------------------

bool SceneSerializer::Save(const std::filesystem::path& path, ForwardRenderer* renderer) {
    if (!renderer) return false;
    const RenderSettings& rs = renderer->GetRenderSettings();

    Writer w;
    auto& os = w.os;

    os << "{\n";
    w.indent = 1;

    // --- renderSettings ---
    w.Indent(); os << "\"renderSettings\": {\n";
    w.indent = 2;
    w.Indent(); os << "\"exposure\": "; w.Float(rs.exposure); os << ",\n";

    w.Indent(); os << "\"bloom\": {";
    os << "\"enabled\": "    << (rs.bloom.enabled ? "true" : "false") << ", ";
    os << "\"threshold\": "; w.Float(rs.bloom.threshold); os << ", ";
    os << "\"strength\": ";  w.Float(rs.bloom.strength);  os << ", ";
    os << "\"iterations\": " << rs.bloom.iterations << "},\n";

    w.Indent(); os << "\"sky\": {";
    os << "\"enabled\": "   << (rs.sky.enabled ? "true" : "false") << ", ";
    os << "\"intensity\": "; w.Float(rs.sky.intensity); os << "},\n";

    w.Indent(); os << "\"ibl\": {";
    os << "\"enabled\": "           << (rs.ibl.enabled ? "true" : "false") << ", ";
    os << "\"diffuseIntensity\": ";  w.Float(rs.ibl.diffuseIntensity);  os << ", ";
    os << "\"specularIntensity\": "; w.Float(rs.ibl.specularIntensity); os << "},\n";

    w.Indent(); os << "\"shadow\": {";
    os << "\"enabled\": "       << (rs.shadow.enabled ? "true" : "false") << ", ";
    os << "\"resolution\": "    << rs.shadow.resolution << ", ";
    os << "\"orthoHalfSize\": "; w.Float(rs.shadow.orthoHalfSize); os << ", ";
    os << "\"nearPlane\": ";     w.Float(rs.shadow.nearPlane);     os << ", ";
    os << "\"farPlane\": ";      w.Float(rs.shadow.farPlane);      os << ", ";
    os << "\"depthBias\": ";     w.Float(rs.shadow.depthBias);     os << ", ";
    os << "\"normalBias\": ";    w.Float(rs.shadow.normalBias);    os << ", ";
    os << "\"pcfKernel\": "      << rs.shadow.pcfKernel << "},\n";

    w.Indent(); os << "\"ssao\": {";
    os << "\"enabled\": "   << (rs.ssao.enabled ? "true" : "false") << ", ";
    os << "\"intensity\": "; w.Float(rs.ssao.intensity); os << ", ";
    os << "\"radius\": ";    w.Float(rs.ssao.radius);    os << ", ";
    os << "\"bias\": ";      w.Float(rs.ssao.bias);      os << ", ";
    os << "\"samples\": "    << rs.ssao.samples << "},\n";

    w.Indent(); os << "\"contactShadow\": {";
    os << "\"enabled\": "      << (rs.contactShadow.enabled ? "true" : "false") << ", ";
    os << "\"steps\": "        << rs.contactShadow.steps << ", ";
    os << "\"maxDistance\": "; w.Float(rs.contactShadow.maxDistance); os << ", ";
    os << "\"thickness\": ";   w.Float(rs.contactShadow.thickness);   os << ", ";
    os << "\"strength\": ";    w.Float(rs.contactShadow.strength);    os << "},\n";

    w.Indent(); os << "\"fxaa\": {";
    os << "\"enabled\": " << (rs.fxaa.enabled ? "true" : "false") << "},\n";

    w.Indent(); os << "\"taa\": {";
    os << "\"enabled\": "  << (rs.taa.enabled ? "true" : "false") << ", ";
    os << "\"blendNew\": "; w.Float(rs.taa.blendNew); os << "},\n";

    w.Indent(); os << "\"ssr\": {";
    os << "\"enabled\": "      << (rs.ssr.enabled ? "true" : "false") << ", ";
    os << "\"maxDistance\": "; w.Float(rs.ssr.maxDistance); os << ", ";
    os << "\"steps\": "        << rs.ssr.steps << ", ";
    os << "\"thickness\": ";   w.Float(rs.ssr.thickness); os << ", ";
    os << "\"fadeEdge\": ";    w.Float(rs.ssr.fadeEdge);  os << "},\n";

    w.Indent(); os << "\"tonemap\": {";
    os << "\"mode\": " << rs.tonemap.mode << "},\n";

    w.Indent(); os << "\"msaa\": {";
    os << "\"samples\": " << rs.msaa.samples << "},\n";

    w.Indent(); os << "\"fog\": {";
    os << "\"enabled\": "       << (rs.fog.enabled ? "true" : "false") << ", ";
    os << "\"density\": ";       w.Float(rs.fog.density);       os << ", ";
    os << "\"heightStart\": ";   w.Float(rs.fog.heightStart);   os << ", ";
    os << "\"heightFalloff\": "; w.Float(rs.fog.heightFalloff); os << ", ";
    os << "\"maxOpacity\": ";    w.Float(rs.fog.maxOpacity);    os << ", ";
    os << "\"color\": [";
    w.Float(rs.fog.color[0]); os << ", ";
    w.Float(rs.fog.color[1]); os << ", ";
    w.Float(rs.fog.color[2]); os << "]}\n";

    w.indent = 1;
    w.Indent(); os << "},\n";

    // --- lights ---
    w.Indent(); os << "\"lights\": [\n";
    w.indent = 2;

    const auto& lights = Light::GetAllLights();
    for (size_t i = 0; i < lights.size(); ++i) {
        Light* light = lights[i];
        auto* owner = light->GetOwner();
        if (!owner) continue;

        Transform& tr = const_cast<Transform&>(owner->GetTransform());
        glm::vec3 pos   = tr.GetLocalPosition();
        glm::vec3 euler = glm::degrees(QuatToEuler(tr.GetLocalRotation()));

        w.Indent(); os << "{";
        os << "\"name\": "; w.Str(owner->GetName()); os << ", ";
        os << "\"type\": "; w.Str(TypeToStr(light->GetType())); os << ", ";
        os << "\"color\": ";    w.Vec3(light->GetColor());    os << ", ";
        os << "\"intensity\": "; w.Float(light->GetIntensity()); os << ", ";
        os << "\"ambient\": ";  w.Vec3(light->GetAmbient());  os << ", ";
        os << "\"position\": "; w.Vec3(pos);                  os << ", ";
        os << "\"rotationEuler\": "; w.Vec3(euler);           os << ", ";
        os << "\"range\": ";    w.Float(light->GetRange());    os << ", ";
        os << "\"constant\": "; w.Float(light->GetConstant()); os << ", ";
        os << "\"linear\": ";   w.Float(light->GetLinear());   os << ", ";
        os << "\"quadratic\": ";w.Float(light->GetQuadratic());os << ", ";
        os << "\"innerAngle\": ";w.Float(light->GetSpotInnerAngle()); os << ", ";
        os << "\"outerAngle\": ";w.Float(light->GetSpotOuterAngle());
        os << "}";
        os << (i + 1 < lights.size() ? ",\n" : "\n");
    }

    w.indent = 1;
    w.Indent(); os << "]\n";
    os << "}\n";

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        ARK_LOG_ERROR("Render", std::string("SceneSerializer: cannot open for write: ") + path.string());
        return false;
    }
    f << os.str();
    ARK_LOG_INFO("Render", std::string("SceneSerializer: saved ") + path.string());
    return true;
}

bool SceneSerializer::Load(const std::filesystem::path& path, ForwardRenderer* renderer) {
    if (!renderer) return false;

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ARK_LOG_WARN("Render", std::string("SceneSerializer: file not found: ") + path.string());
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();

    Parser parser(src);
    auto root = parser.ParseRoot();
    if (!root || !root->IsObject()) {
        ARK_LOG_ERROR("Render", std::string("SceneSerializer: parse error in ") + path.string());
        return false;
    }
    const JObject& rootObj = root->AsObject();

    // --- renderSettings ---
    RenderSettings& rs = renderer->GetRenderSettings();
    if (const JValue* vs = Find(rootObj, "renderSettings"); vs && vs->IsObject()) {
        const JObject& o = vs->AsObject();
        rs.exposure = FloatOr(o, "exposure", rs.exposure);
        if (const JValue* b = Find(o, "bloom"); b && b->IsObject()) {
            const JObject& bo = b->AsObject();
            rs.bloom.enabled    = BoolOr (bo, "enabled",    rs.bloom.enabled);
            rs.bloom.threshold  = FloatOr(bo, "threshold",  rs.bloom.threshold);
            rs.bloom.strength   = FloatOr(bo, "strength",   rs.bloom.strength);
            rs.bloom.iterations = IntOr  (bo, "iterations", rs.bloom.iterations);
        }
        if (const JValue* sk = Find(o, "sky"); sk && sk->IsObject()) {
            const JObject& so = sk->AsObject();
            rs.sky.enabled   = BoolOr (so, "enabled",   rs.sky.enabled);
            rs.sky.intensity = FloatOr(so, "intensity", rs.sky.intensity);
        }
        if (const JValue* ib = Find(o, "ibl"); ib && ib->IsObject()) {
            const JObject& io = ib->AsObject();
            rs.ibl.enabled           = BoolOr (io, "enabled",           rs.ibl.enabled);
            rs.ibl.diffuseIntensity  = FloatOr(io, "diffuseIntensity",  rs.ibl.diffuseIntensity);
            rs.ibl.specularIntensity = FloatOr(io, "specularIntensity", rs.ibl.specularIntensity);
        }
        if (const JValue* sh = Find(o, "shadow"); sh && sh->IsObject()) {
            const JObject& so = sh->AsObject();
            rs.shadow.enabled       = BoolOr (so, "enabled",       rs.shadow.enabled);
            rs.shadow.resolution    = IntOr  (so, "resolution",    rs.shadow.resolution);
            rs.shadow.orthoHalfSize = FloatOr(so, "orthoHalfSize", rs.shadow.orthoHalfSize);
            rs.shadow.nearPlane     = FloatOr(so, "nearPlane",     rs.shadow.nearPlane);
            rs.shadow.farPlane      = FloatOr(so, "farPlane",      rs.shadow.farPlane);
            rs.shadow.depthBias     = FloatOr(so, "depthBias",     rs.shadow.depthBias);
            rs.shadow.normalBias    = FloatOr(so, "normalBias",    rs.shadow.normalBias);
            rs.shadow.pcfKernel     = IntOr  (so, "pcfKernel",     rs.shadow.pcfKernel);
        }
        if (const JValue* sa = Find(o, "ssao"); sa && sa->IsObject()) {
            const JObject& so = sa->AsObject();
            rs.ssao.enabled   = BoolOr (so, "enabled",   rs.ssao.enabled);
            rs.ssao.intensity = FloatOr(so, "intensity", rs.ssao.intensity);
            rs.ssao.radius    = FloatOr(so, "radius",    rs.ssao.radius);
            rs.ssao.bias      = FloatOr(so, "bias",      rs.ssao.bias);
            rs.ssao.samples   = IntOr  (so, "samples",   rs.ssao.samples);
        }
        if (const JValue* cs = Find(o, "contactShadow"); cs && cs->IsObject()) {
            const JObject& co = cs->AsObject();
            rs.contactShadow.enabled     = BoolOr (co, "enabled",     rs.contactShadow.enabled);
            rs.contactShadow.steps       = IntOr  (co, "steps",       rs.contactShadow.steps);
            rs.contactShadow.maxDistance = FloatOr(co, "maxDistance", rs.contactShadow.maxDistance);
            rs.contactShadow.thickness   = FloatOr(co, "thickness",   rs.contactShadow.thickness);
            rs.contactShadow.strength    = FloatOr(co, "strength",    rs.contactShadow.strength);
        }
        if (const JValue* fx = Find(o, "fxaa"); fx && fx->IsObject()) {
            rs.fxaa.enabled = BoolOr(fx->AsObject(), "enabled", rs.fxaa.enabled);
        }
        if (const JValue* ta = Find(o, "taa"); ta && ta->IsObject()) {
            const JObject& to = ta->AsObject();
            rs.taa.enabled  = BoolOr (to, "enabled",  rs.taa.enabled);
            rs.taa.blendNew = FloatOr(to, "blendNew", rs.taa.blendNew);
        }
        if (const JValue* sr = Find(o, "ssr"); sr && sr->IsObject()) {
            const JObject& so = sr->AsObject();
            rs.ssr.enabled     = BoolOr (so, "enabled",     rs.ssr.enabled);
            rs.ssr.maxDistance = FloatOr(so, "maxDistance", rs.ssr.maxDistance);
            rs.ssr.steps       = IntOr  (so, "steps",       rs.ssr.steps);
            rs.ssr.thickness   = FloatOr(so, "thickness",   rs.ssr.thickness);
            rs.ssr.fadeEdge    = FloatOr(so, "fadeEdge",    rs.ssr.fadeEdge);
        }
        if (const JValue* tm = Find(o, "tonemap"); tm && tm->IsObject()) {
            rs.tonemap.mode = IntOr(tm->AsObject(), "mode", rs.tonemap.mode);
        }
        if (const JValue* ma = Find(o, "msaa"); ma && ma->IsObject()) {
            rs.msaa.samples = IntOr(ma->AsObject(), "samples", rs.msaa.samples);
        }
        if (const JValue* fg = Find(o, "fog"); fg && fg->IsObject()) {
            const JObject& fo = fg->AsObject();
            rs.fog.enabled       = BoolOr (fo, "enabled",       rs.fog.enabled);
            rs.fog.density       = FloatOr(fo, "density",       rs.fog.density);
            rs.fog.heightStart   = FloatOr(fo, "heightStart",   rs.fog.heightStart);
            rs.fog.heightFalloff = FloatOr(fo, "heightFalloff", rs.fog.heightFalloff);
            rs.fog.maxOpacity    = FloatOr(fo, "maxOpacity",    rs.fog.maxOpacity);
            if (const JValue* cv = Find(fo, "color"); cv && cv->IsArray()) {
                const auto& arr = cv->AsArray();
                if (arr.size() >= 3 && arr[0].IsNumber() && arr[1].IsNumber() && arr[2].IsNumber()) {
                    rs.fog.color[0] = (float)arr[0].AsNumber();
                    rs.fog.color[1] = (float)arr[1].AsNumber();
                    rs.fog.color[2] = (float)arr[2].AsNumber();
                }
            }
        }
    }

    // --- lights: match by owner name ---
    if (const JValue* vl = Find(rootObj, "lights"); vl && vl->IsArray()) {
        // Build name -> Light* map from runtime.
        std::unordered_map<std::string, Light*> byName;
        for (Light* light : Light::GetAllLights()) {
            auto* owner = light->GetOwner();
            if (owner) byName[owner->GetName()] = light;
        }

        int matched = 0;
        for (const JValue& entry : vl->AsArray()) {
            if (!entry.IsObject()) continue;
            const JObject& lo = entry.AsObject();
            std::string name = StringOr(lo, "name", "");
            auto it = byName.find(name);
            if (it == byName.end()) continue;
            Light* light = it->second;

            light->SetType     (StrToType(StringOr(lo, "type", "Directional")));
            light->SetColor    (Vec3Or   (lo, "color",   light->GetColor()));
            light->SetIntensity(FloatOr  (lo, "intensity", light->GetIntensity()));
            light->SetAmbient  (Vec3Or   (lo, "ambient", light->GetAmbient()));
            light->SetRange    (FloatOr  (lo, "range",     light->GetRange()));
            light->SetConstant (FloatOr  (lo, "constant",  light->GetConstant()));
            light->SetLinear   (FloatOr  (lo, "linear",    light->GetLinear()));
            light->SetQuadratic(FloatOr  (lo, "quadratic", light->GetQuadratic()));
            light->SetSpotAngles(FloatOr(lo, "innerAngle", light->GetSpotInnerAngle()),
                                 FloatOr(lo, "outerAngle", light->GetSpotOuterAngle()));

            auto* owner = light->GetOwner();
            if (owner) {
                Transform& tr = const_cast<Transform&>(owner->GetTransform());
                tr.SetLocalPosition(Vec3Or(lo, "position",      tr.GetLocalPosition()));
                tr.SetLocalRotation(EulerToQuat(Vec3Or(lo, "rotationEuler",
                    glm::degrees(QuatToEuler(tr.GetLocalRotation())))));
            }
            ++matched;
        }
        ARK_LOG_INFO("Render", std::string("SceneSerializer: loaded ") + path.string() +
                               " (" + std::to_string(matched) + " lights matched)");
    } else {
        ARK_LOG_INFO("Render", std::string("SceneSerializer: loaded ") + path.string() +
                               " (no lights section)");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Hot reload
// ---------------------------------------------------------------------------

namespace {
    std::filesystem::path g_hotPath;
    std::filesystem::file_time_type g_hotMTime{};
    bool g_hotArmed = false;
    bool g_hotFirstTick = false;
}

void SceneSerializer::EnableHotReload(const std::filesystem::path& path) {
    g_hotPath = path;
    g_hotArmed = !path.empty();
    g_hotFirstTick = g_hotArmed;
    g_hotMTime = {};
}

void SceneSerializer::Tick(ForwardRenderer* renderer) {
    if (!g_hotArmed || g_hotPath.empty() || !renderer) return;
    std::error_code ec;

    // First tick: either load an existing file, or write an initial snapshot
    // (light components have had a chance to Init() by now).
    if (g_hotFirstTick) {
        g_hotFirstTick = false;
        if (std::filesystem::exists(g_hotPath, ec)) {
            Load(g_hotPath, renderer);
        } else {
            Save(g_hotPath, renderer);
        }
        if (std::filesystem::exists(g_hotPath, ec)) {
            g_hotMTime = std::filesystem::last_write_time(g_hotPath, ec);
        }
        return;
    }

    if (!std::filesystem::exists(g_hotPath, ec)) return;
    auto mt = std::filesystem::last_write_time(g_hotPath, ec);
    if (ec) return;
    if (mt != g_hotMTime) {
        g_hotMTime = mt;
        Load(g_hotPath, renderer);
    }
}

} // namespace ark
