// engine/core/SchemaHash.cpp — see header for algorithm.

#include "engine/core/SchemaHash.h"
#include "engine/core/TypeInfo.h"
#include "engine/util/Sha256.h"

#include <algorithm>
#include <vector>

namespace ark {

std::string ComputeSchemaHash(const TypeInfo& type) {
    // Build "<name>:<type>" entries for every field, sort, join with '\n'.
    std::vector<std::string> entries;
    entries.reserve(type.fields.size());
    for (const auto& f : type.fields) {
        entries.emplace_back(f.name + ":" + FieldTypeName(f.type));
    }
    std::sort(entries.begin(), entries.end());

    std::string buf;
    buf.reserve(entries.size() * 32);
    for (const auto& e : entries) {
        buf.append(e);
        buf.push_back('\n');
    }
    return Sha256::HashHex16(buf);
}

std::string ComputeRegistrySchemaHash() {
    const auto& all = TypeRegistry::Get().All();
    std::vector<std::string> entries;
    entries.reserve(all.size());
    for (const auto& t : all) {
        entries.emplace_back(t.name + ":" + ComputeSchemaHash(t));
    }
    std::sort(entries.begin(), entries.end());

    std::string buf;
    buf.reserve(entries.size() * 32);
    for (const auto& e : entries) {
        buf.append(e);
        buf.push_back('\n');
    }
    return Sha256::HashHex16(buf);
}

} // namespace ark
