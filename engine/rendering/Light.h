#pragma once

#include "engine/core/AComponent.h"
#include <glm/glm.hpp>
#include <vector>

namespace ark {

class Light : public AComponent {
public:
    enum class Type { Directional, Point, Spot };

    void OnAttach() override;
    void OnDetach() override;

    // --- Type ---
    Type GetType() const { return type_; }
    void SetType(Type t) { type_ = t; }

    // --- Color & intensity ---
    const glm::vec3& GetColor() const { return color_; }
    void SetColor(const glm::vec3& color) { color_ = color; }
    float GetIntensity() const { return intensity_; }
    void SetIntensity(float i) { intensity_ = i; }

    // --- Point/Spot attenuation ---
    float GetRange() const { return range_; }
    void SetRange(float r) { range_ = r; }

    // --- Spot light ---
    float GetSpotInnerAngle() const { return spotInnerAngle_; }
    float GetSpotOuterAngle() const { return spotOuterAngle_; }
    void SetSpotAngles(float innerDeg, float outerDeg) {
        spotInnerAngle_ = innerDeg;
        spotOuterAngle_ = outerDeg;
    }

    // --- Ambient contribution ---
    const glm::vec3& GetAmbient() const { return ambient_; }
    void SetAmbient(const glm::vec3& a) { ambient_ = a; }

    // --- Static registry ---
    static const std::vector<Light*>& GetAllLights();

private:
    static std::vector<Light*> allLights_;

    Type type_ = Type::Directional;
    glm::vec3 color_{1.0f};
    float intensity_ = 1.0f;
    float range_ = 10.0f;
    float spotInnerAngle_ = 12.5f;
    float spotOuterAngle_ = 17.5f;
    glm::vec3 ambient_{0.1f};
};

} // namespace ark
