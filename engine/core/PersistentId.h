#pragma once

// -----------------------------------------------------------------------------
// PersistentId — v0.3 ModSpec §4 持久身份格式校验
//
// Format: "<mod_id>:<local_id>"
//   - mod_id   = [a-z][a-z0-9_]*                   (e.g. "core", "my_addon")
//   - local_id = [a-z][a-z0-9_]*(\.[a-z0-9_]+)*    (e.g. "cottage_door",
//                                                   "item.iron_sword")
//
// Both segments are forced lowercase ASCII so that:
//   - Cross-platform filesystem usage stays predictable.
//   - String comparison is the single canonical equality check.
//
// Backwards compat: legacy UUID v4 guids ("xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx")
// remain accepted on load — IsValidLegacyUuid() catches them so SceneDoc can
// emit a deprecation WARN without rejecting the file. v0.3 dogfood scenes
// (cottage.toml, hellomod overlay) MUST use the new persistent format.
// -----------------------------------------------------------------------------

#include <string>
#include <string_view>

namespace ark {

// Returns true iff `id` is a valid mod_id segment under the spec's grammar.
bool IsValidModIdSegment(std::string_view id);

// Returns true iff `id` is a valid local_id segment under the spec's grammar.
bool IsValidLocalIdSegment(std::string_view id);

// Returns true iff `id` matches "<mod_id>:<local_id>".
bool IsValidPersistentId(std::string_view id);

// Returns true iff `id` matches the canonical UUID v4 pattern. Used for
// classifying legacy guids without rejecting them.
bool IsValidLegacyUuid(std::string_view id);

// Splits "<mod>:<local>" into its two halves. On malformed input, both
// outparams are cleared and the function returns false.
bool SplitPersistentId(std::string_view id,
                       std::string* outMod,
                       std::string* outLocal);

} // namespace ark
