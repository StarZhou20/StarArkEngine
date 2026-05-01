#pragma once

#include <vector>

namespace ark {

class Camera;
class MeshRenderer;

/// Shared visibility + sort pass used by ForwardRenderer and DeferredRenderer.
/// Walks the global `MeshRenderer::GetAllRenderers()` registry and emits the
/// subset that is:
///   * owner alive & active in hierarchy,
///   * `IsEnabled()`,
///   * has a non-null mesh + material + Forward-slot shader.
///
/// `includeTransparent` controls whether materials whose `IsTransparent()`
/// returns true are emitted. The deferred geometry pass passes false; the
/// forward overlay pass passes true. The forward legacy single-list path
/// passes true (everything in one shader-sorted list, original behaviour).
///
/// Output is sorted by (shader*, material*) to minimise state switches.
/// `camera` is currently unused (no frustum cull yet) but is part of the
/// signature so future passes can plug in cull data without churn.
void CollectOpaqueDrawList(Camera* camera,
                           std::vector<MeshRenderer*>& out,
                           bool includeTransparent = false);

/// Collect renderers whose material's `IsTransparent()` is true. Sorted
/// **back-to-front** by world-space distance to `camera`'s view position
/// for correct alpha blending.
void CollectTransparentDrawList(Camera* camera,
                                std::vector<MeshRenderer*>& out);

} // namespace ark
