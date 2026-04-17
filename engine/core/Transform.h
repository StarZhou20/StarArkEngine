#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <algorithm>

namespace ark {

class AObject;

class Transform {
public:
    explicit Transform(AObject* owner);
    ~Transform();

    Transform(const Transform&) = delete;
    Transform& operator=(const Transform&) = delete;

    // --- Local transform ---
    const glm::vec3& GetLocalPosition() const { return localPosition_; }
    const glm::quat& GetLocalRotation() const { return localRotation_; }
    const glm::vec3& GetLocalScale() const { return localScale_; }

    void SetLocalPosition(const glm::vec3& pos);
    void SetLocalRotation(const glm::quat& rot);
    void SetLocalScale(const glm::vec3& scale);

    // --- World transform (computed from hierarchy) ---
    glm::vec3 GetWorldPosition() const;
    const glm::mat4& GetWorldMatrix();

    // --- Hierarchy ---
    AObject* GetOwner() const { return owner_; }
    Transform* GetParent() const { return parent_; }
    const std::vector<Transform*>& GetChildren() const { return children_; }

    void SetParent(Transform* newParent);
    void AddChild(Transform* child);
    void RemoveChild(Transform* child);

    // --- Dirty flag system ---
    bool IsDirty() const { return dirty_; }
    void MarkDirty();
    void UpdateWorldMatrix();

private:
    void RecalculateLocalMatrix();

    AObject* owner_;
    Transform* parent_ = nullptr;
    std::vector<Transform*> children_;

    glm::vec3 localPosition_{0.0f};
    glm::quat localRotation_{glm::identity<glm::quat>()};
    glm::vec3 localScale_{1.0f};

    glm::mat4 localMatrix_{1.0f};
    glm::mat4 worldMatrix_{1.0f};
    bool dirty_ = true;
};

} // namespace ark
