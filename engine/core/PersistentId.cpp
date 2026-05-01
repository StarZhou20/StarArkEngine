#include "PersistentId.h"

namespace ark {
namespace {

inline bool IsLowerAlpha(char c)  { return c >= 'a' && c <= 'z'; }
inline bool IsDigit(char c)       { return c >= '0' && c <= '9'; }
inline bool IsLowerAlnum(char c)  { return IsLowerAlpha(c) || IsDigit(c); }

// segment::= [a-z][a-z0-9_]*
bool IsModIdShape(std::string_view s) {
    if (s.empty()) return false;
    if (!IsLowerAlpha(s[0])) return false;
    for (std::size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        if (!(IsLowerAlnum(c) || c == '_')) return false;
    }
    return true;
}

// localSegment::= [a-z][a-z0-9_]*  (no dots — used as part of local_id parts)
bool IsLocalSegmentShape(std::string_view s, bool firstPart) {
    if (s.empty()) return false;
    const bool firstCharOk = firstPart ? IsLowerAlpha(s[0])
                                       : (IsLowerAlnum(s[0]) || s[0] == '_');
    if (!firstCharOk) return false;
    for (std::size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        if (!(IsLowerAlnum(c) || c == '_')) return false;
    }
    return true;
}

} // namespace

bool IsValidModIdSegment(std::string_view id) {
    return IsModIdShape(id);
}

bool IsValidLocalIdSegment(std::string_view id) {
    if (id.empty()) return false;
    // Split on '.', validate each subsegment. First subsegment must start
    // with a letter; subsequent may start with letter/digit/underscore.
    std::size_t start = 0;
    bool first = true;
    for (std::size_t i = 0; i <= id.size(); ++i) {
        if (i == id.size() || id[i] == '.') {
            const std::string_view part = id.substr(start, i - start);
            if (!IsLocalSegmentShape(part, first)) return false;
            first = false;
            start = i + 1;
        }
    }
    return true;
}

bool IsValidPersistentId(std::string_view id) {
    const auto colon = id.find(':');
    if (colon == std::string_view::npos) return false;
    return IsValidModIdSegment(id.substr(0, colon))
        && IsValidLocalIdSegment(id.substr(colon + 1));
}

bool IsValidLegacyUuid(std::string_view id) {
    // Pattern: 8-4-4-4-12 hex, where the 13th nibble (start of group 3) is '4'
    // and the 17th nibble (start of group 4) is one of 8/9/a/b.
    if (id.size() != 36) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
            continue;
        }
        const bool hex = IsDigit(c)
                      || (c >= 'a' && c <= 'f')
                      || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    if (id[14] != '4') return false;
    const char v = id[19];
    if (!(v == '8' || v == '9' || v == 'a' || v == 'b'
                                || v == 'A' || v == 'B')) return false;
    return true;
}

bool SplitPersistentId(std::string_view id,
                       std::string* outMod,
                       std::string* outLocal) {
    if (outMod)   outMod->clear();
    if (outLocal) outLocal->clear();
    if (!IsValidPersistentId(id)) return false;
    const auto colon = id.find(':');
    if (outMod)   outMod->assign(id.substr(0, colon));
    if (outLocal) outLocal->assign(id.substr(colon + 1));
    return true;
}

} // namespace ark
