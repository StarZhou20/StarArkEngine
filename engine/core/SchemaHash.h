// engine/core/SchemaHash.h — v0.3 ModSpec §6.2 schema_hash for reflected types.
//
// Algorithm (per ModSpec §6.2):
//   - Input: sorted list of "<field_serial_name>:<field_type_name>" for each
//            **required** field in a reflected TypeInfo. Joined with '\n'.
//   - Hash : SHA-256, hex digest, first 16 chars.
//   - Stable across:
//       * field declaration order (sort)
//       * insertion of new optional fields (we don't have an "Optional"
//         concept yet — *every* reflected field is treated as required for
//         now, so any field add/remove changes the hash. Once Optional is
//         introduced, exclude it from the input set).
//
// Used by:
//   - Save header active_mods[].schema_hash (planned, ModSpec §6.1)
//   - Asset cache invalidation tied to reflected-component layout
#pragma once

#include <string>

namespace ark {

struct TypeInfo;

// Returns the 16-char schema hash of a single reflected type.
std::string ComputeSchemaHash(const TypeInfo& type);

// Returns the 16-char schema hash of the entire reflection registry,
// i.e. SHA-256(sorted([typeName + ":" + ComputeSchemaHash(t) for each type])).
// Intended for save-header diffing where mod identity doesn't carry per-type
// granularity.
std::string ComputeRegistrySchemaHash();

} // namespace ark
