#include "engine/serialization/TomlDoc.h"

#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace ark {

// =============================================================================
// TomlValue
// =============================================================================

TomlValue::TomlValue(const TomlValue& other)
    : kind_(other.kind_), b_(other.b_), i_(other.i_), f_(other.f_),
      s_(other.s_), arr_(other.arr_),
      table_(other.table_ ? std::make_unique<TomlTable>(*other.table_) : nullptr)
{}

TomlValue& TomlValue::operator=(const TomlValue& other) {
    if (this != &other) {
        TomlValue tmp(other);
        *this = std::move(tmp);
    }
    return *this;
}

TomlValue TomlValue::Bool(bool b)              { TomlValue v; v.kind_ = Kind::Bool;   v.b_ = b; return v; }
TomlValue TomlValue::Int(std::int64_t i)       { TomlValue v; v.kind_ = Kind::Int;    v.i_ = i; return v; }
TomlValue TomlValue::Float(double d)           { TomlValue v; v.kind_ = Kind::Float;  v.f_ = d; return v; }
TomlValue TomlValue::String(std::string s)     { TomlValue v; v.kind_ = Kind::String; v.s_ = std::move(s); return v; }
TomlValue TomlValue::Array(TomlArray arr)      { TomlValue v; v.kind_ = Kind::Array;  v.arr_ = std::move(arr); return v; }
TomlValue TomlValue::Table(TomlTable t) {
    TomlValue v;
    v.kind_ = Kind::Table;
    v.table_ = std::make_unique<TomlTable>(std::move(t));
    return v;
}

const TomlTable& TomlValue::AsTable() const { assert(table_); return *table_; }
TomlTable&       TomlValue::AsTable()       { assert(table_); return *table_; }

// =============================================================================
// TomlTable — copy ctor needs to deep-copy unique_ptr children
// =============================================================================

TomlTable::TomlTable(const TomlTable& other)
    : keyOrder_(other.keyOrder_),
      tableOrder_(other.tableOrder_),
      aotOrder_(other.aotOrder_)
{
    for (const auto& [k, v] : other.values_) values_.emplace(k, v);
    for (const auto& [k, up] : other.tables_) {
        tables_.emplace(k, std::make_unique<TomlTable>(*up));
    }
    for (const auto& [k, up] : other.aots_) {
        auto a = std::make_unique<ArrayOfTables>();
        for (const auto& t : up->tables_) {
            a->tables_.push_back(std::make_unique<TomlTable>(*t));
        }
        aots_.emplace(k, std::move(a));
    }
}

TomlTable& TomlTable::operator=(const TomlTable& other) {
    if (this != &other) {
        TomlTable tmp(other);
        *this = std::move(tmp);
    }
    return *this;
}

void TomlTable::Set(std::string_view key, TomlValue value) {
    std::string k(key);
    auto it = values_.find(k);
    if (it == values_.end()) {
        keyOrder_.push_back(k);
    }
    values_.insert_or_assign(k, std::move(value));
}

const TomlValue* TomlTable::Find(std::string_view key) const {
    auto it = values_.find(key);
    return it == values_.end() ? nullptr : &it->second;
}
TomlValue* TomlTable::Find(std::string_view key) {
    auto it = values_.find(key);
    return it == values_.end() ? nullptr : &it->second;
}

TomlTable& TomlTable::GetOrCreateTable(std::string_view key) {
    auto it = tables_.find(key);
    if (it == tables_.end()) {
        auto [ins, ok] = tables_.emplace(std::string(key), std::make_unique<TomlTable>());
        tableOrder_.emplace_back(key);
        return *ins->second;
    }
    return *it->second;
}

const TomlTable* TomlTable::FindTable(std::string_view key) const {
    auto it = tables_.find(key);
    return it == tables_.end() ? nullptr : it->second.get();
}

const TomlTable* TomlTable::GetTableByName(std::string_view key) const {
    return FindTable(key);
}

TomlTable& TomlTable::ArrayOfTables::Append() {
    tables_.push_back(std::make_unique<TomlTable>());
    return *tables_.back();
}

TomlTable::ArrayOfTables& TomlTable::GetOrCreateArrayOfTables(std::string_view key) {
    auto it = aots_.find(key);
    if (it == aots_.end()) {
        auto [ins, ok] = aots_.emplace(std::string(key), std::make_unique<ArrayOfTables>());
        aotOrder_.emplace_back(key);
        return *ins->second;
    }
    return *it->second;
}

const TomlTable::ArrayOfTables* TomlTable::FindArrayOfTables(std::string_view key) const {
    auto it = aots_.find(key);
    return it == aots_.end() ? nullptr : it->second.get();
}

TomlTable::ArrayOfTables* TomlTable::FindArrayOfTables(std::string_view key) {
    auto it = aots_.find(key);
    return it == aots_.end() ? nullptr : it->second.get();
}

// =============================================================================
// Dump
// =============================================================================

namespace {

void DumpString(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    os << buf;
                } else {
                    os << c;
                }
        }
    }
    os << '"';
}

void DumpFloat(std::ostringstream& os, double d) {
    if (std::isnan(d))  { os << "nan"; return; }
    if (std::isinf(d))  { os << (d < 0 ? "-inf" : "inf"); return; }
    // 固定精度 + 去尾零；始终保留一个小数点
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    std::string s(buf);
    if (s.find_first_of(".eEnN") == std::string::npos) {
        s += ".0";
    }
    os << s;
}

void DumpValue(std::ostringstream& os, const TomlValue& v);

void DumpArray(std::ostringstream& os, const TomlArray& a) {
    os << '[';
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) os << ", ";
        DumpValue(os, a[i]);
    }
    os << ']';
}

void DumpValue(std::ostringstream& os, const TomlValue& v) {
    switch (v.GetKind()) {
        case TomlValue::Kind::Bool:   os << (v.AsBool() ? "true" : "false"); break;
        case TomlValue::Kind::Int:    os << v.AsInt(); break;
        case TomlValue::Kind::Float:  DumpFloat(os, v.AsFloat()); break;
        case TomlValue::Kind::String: DumpString(os, v.AsString()); break;
        case TomlValue::Kind::Array:  DumpArray(os, v.AsArray()); break;
        case TomlValue::Kind::Null:
        case TomlValue::Kind::Table:
        default:
            // 表级别由 DumpTable 处理；Null 不应出现
            os << "\"\"";
            break;
    }
}

void DumpTable(std::ostringstream& os, const TomlTable& t, const std::string& path);

void DumpTableBody(std::ostringstream& os, const TomlTable& t, const std::string& path) {
    // 1) 标量 / 数组
    for (const auto& key : t.Keys()) {
        const TomlValue* v = t.Find(key);
        if (!v) continue;
        os << key << " = ";
        DumpValue(os, *v);
        os << '\n';
    }
    // 2) 子表
    for (const auto& key : t.TableKeys()) {
        const TomlTable* sub = t.FindTable(key);
        if (!sub) continue;
        std::string subPath = path.empty() ? key : (path + "." + key);
        os << '\n' << '[' << subPath << "]\n";
        DumpTableBody(os, *sub, subPath);
    }
    // 3) 数组表
    for (const auto& key : t.AotKeys()) {
        const TomlTable::ArrayOfTables* a = t.FindArrayOfTables(key);
        if (!a) continue;
        std::string subPath = path.empty() ? key : (path + "." + key);
        for (std::size_t i = 0; i < a->Size(); ++i) {
            os << '\n' << "[[" << subPath << "]]\n";
            DumpTableBody(os, (*a)[i], subPath);
        }
    }
}

} // anonymous namespace

std::string TomlDoc::Dump() const {
    std::ostringstream os;
    DumpTableBody(os, root_, "");
    return os.str();
}

// =============================================================================
// Parser
// =============================================================================

namespace {

struct Parser {
    std::string_view src;
    std::size_t pos = 0;
    int line = 1;
    std::string errMsg;
    int errLine = 0;

    bool Eof() const { return pos >= src.size(); }
    char Peek() const { return pos < src.size() ? src[pos] : '\0'; }
    char Next() {
        char c = src[pos++];
        if (c == '\n') ++line;
        return c;
    }

    void Fail(std::string msg) {
        if (errMsg.empty()) { errMsg = std::move(msg); errLine = line; }
    }
    bool Ok() const { return errMsg.empty(); }

    void SkipLineComment() {
        while (!Eof() && Peek() != '\n') ++pos;
    }
    // 跳过空白（含换行）与注释
    void SkipWsAndComments() {
        while (!Eof()) {
            char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { Next(); }
            else if (c == '#') { SkipLineComment(); }
            else break;
        }
    }
    // 跳过行内空白（不跨行）
    void SkipInlineWs() {
        while (!Eof()) {
            char c = Peek();
            if (c == ' ' || c == '\t') ++pos;
            else if (c == '#') { SkipLineComment(); }
            else break;
        }
    }

    bool IsKeyChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    }

    // 解析裸 key 段（不含 '.'）
    std::string ParseKeySegment() {
        std::string out;
        if (Peek() == '"') {
            // 引号 key
            Next(); // "
            while (!Eof() && Peek() != '"') {
                char c = Next();
                if (c == '\\' && !Eof()) { out += Next(); } else out += c;
            }
            if (Peek() == '"') Next(); else Fail("unterminated quoted key");
        } else {
            while (!Eof() && IsKeyChar(Peek())) out += Next();
        }
        return out;
    }

    // 解析点分 key 列表，如 "transform.position"
    std::vector<std::string> ParseDottedKey() {
        std::vector<std::string> segs;
        SkipInlineWs();
        segs.push_back(ParseKeySegment());
        while (Ok()) {
            SkipInlineWs();
            if (Peek() != '.') break;
            Next();
            SkipInlineWs();
            segs.push_back(ParseKeySegment());
        }
        return segs;
    }

    // 解析字符串字面量
    std::string ParseString() {
        assert(Peek() == '"');
        Next();
        std::string out;
        while (!Eof() && Peek() != '"') {
            char c = Next();
            if (c == '\\' && !Eof()) {
                char esc = Next();
                switch (esc) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        // 4-hex codepoint → UTF-8（仅 BMP 基本支持）
                        if (pos + 4 > src.size()) { Fail("bad \\u escape"); return out; }
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = Next();
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else { Fail("bad hex in \\u"); return out; }
                        }
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += esc; break;
                }
            } else {
                out += c;
            }
        }
        if (Peek() == '"') Next(); else Fail("unterminated string");
        return out;
    }

    TomlValue ParseNumberOrBool() {
        // 尝试 bool
        if (src.compare(pos, 4, "true") == 0) {
            pos += 4; return TomlValue::Bool(true);
        }
        if (src.compare(pos, 5, "false") == 0) {
            pos += 5; return TomlValue::Bool(false);
        }
        // 数字
        std::size_t start = pos;
        if (Peek() == '+' || Peek() == '-') ++pos;
        bool isFloat = false;
        while (!Eof()) {
            char c = Peek();
            if ((c >= '0' && c <= '9') || c == '_') { ++pos; }
            else if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'i') {
                isFloat = true; ++pos;
            }
            else if ((c == '+' || c == '-') && pos > start &&
                     (src[pos-1] == 'e' || src[pos-1] == 'E')) { ++pos; }
            else break;
        }
        std::string token;
        token.reserve(pos - start);
        for (std::size_t i = start; i < pos; ++i) {
            if (src[i] != '_') token += src[i];
        }
        if (isFloat) {
            if (token == "inf" || token == "+inf") return TomlValue::Float(std::numeric_limits<double>::infinity());
            if (token == "-inf") return TomlValue::Float(-std::numeric_limits<double>::infinity());
            if (token == "nan" || token == "+nan" || token == "-nan") return TomlValue::Float(std::nan(""));
            try {
                return TomlValue::Float(std::stod(token));
            } catch (...) { Fail("bad float: " + token); return {}; }
        } else {
            try {
                return TomlValue::Int(static_cast<std::int64_t>(std::stoll(token)));
            } catch (...) { Fail("bad int: " + token); return {}; }
        }
    }

    TomlValue ParseValue() {
        SkipInlineWs();
        if (Eof()) { Fail("expected value"); return {}; }
        char c = Peek();
        if (c == '"') return TomlValue::String(ParseString());
        if (c == '[') return ParseArray();
        return ParseNumberOrBool();
    }

    TomlValue ParseArray() {
        assert(Peek() == '[');
        Next();
        TomlArray arr;
        SkipWsAndComments();
        if (Peek() == ']') { Next(); return TomlValue::Array(std::move(arr)); }
        while (Ok()) {
            SkipWsAndComments();
            arr.push_back(ParseValue());
            SkipWsAndComments();
            if (Peek() == ',') { Next(); continue; }
            if (Peek() == ']') { Next(); break; }
            Fail("expected , or ] in array");
            break;
        }
        return TomlValue::Array(std::move(arr));
    }

    // 把 dotted-key 路径沿着 currentTable 向下走，最后一段在返回表上 Set
    // 支持 "transform.position = [...]" —— 自动创建中间子表
    void AssignDottedKey(TomlTable* currentTable,
                         const std::vector<std::string>& segs,
                         TomlValue value) {
        TomlTable* t = currentTable;
        for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
            t = &t->GetOrCreateTable(segs[i]);
        }
        t->Set(segs.back(), std::move(value));
    }

    // 根据 [header] 或 [[header]] 的 dotted 段路径，从 root 下钻到目标表
    // isArray = true 表示 [[...]]：最后一段对应 AOT，Append 一个新 table 返回
    // TOML 规则：若中间段对应已存在的 AOT，则下钻到其最后一个 entry。
    // 这样 `[[objects]]` ... `[objects.transform]` / `[[objects.components]]`
    // 会正确附着到最近的 `[[objects]]` 条目上。
    TomlTable* ResolveHeader(TomlTable* root, const std::vector<std::string>& segs, bool isArray) {
        if (segs.empty()) { Fail("empty header"); return nullptr; }
        TomlTable* t = root;
        for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
            const auto& seg = segs[i];
            if (auto* aot = t->FindArrayOfTables(seg); aot && aot->Size() > 0) {
                t = &(*aot)[aot->Size() - 1];
            } else {
                t = &t->GetOrCreateTable(seg);
            }
        }
        if (isArray) {
            auto& aot = t->GetOrCreateArrayOfTables(segs.back());
            return &aot.Append();
        }
        return &t->GetOrCreateTable(segs.back());
    }

    bool Parse(TomlDoc& doc) {
        TomlTable* current = &doc.Root();
        while (Ok() && !Eof()) {
            SkipWsAndComments();
            if (Eof()) break;
            char c = Peek();
            if (c == '[') {
                Next();
                bool isArray = false;
                if (Peek() == '[') { isArray = true; Next(); }
                SkipInlineWs();
                auto segs = ParseDottedKey();
                SkipInlineWs();
                if (Peek() != ']') { Fail("expected ]"); return false; }
                Next();
                if (isArray) {
                    if (Peek() != ']') { Fail("expected ]]"); return false; }
                    Next();
                }
                // header 行后允许空白 + 注释
                SkipInlineWs();
                current = ResolveHeader(&doc.Root(), segs, isArray);
                if (!current) return false;
            } else {
                // key = value
                auto segs = ParseDottedKey();
                if (segs.empty() || segs[0].empty()) { Fail("empty key"); return false; }
                SkipInlineWs();
                if (Peek() != '=') { Fail("expected ="); return false; }
                Next();
                TomlValue val = ParseValue();
                AssignDottedKey(current, segs, std::move(val));
                SkipInlineWs(); // 行尾注释/空白
            }
        }
        return Ok();
    }
};

} // anonymous namespace

std::optional<TomlDoc> TomlDoc::Parse(std::string_view text,
                                      std::string* errorMsg,
                                      int* errorLine) {
    Parser p{text};
    TomlDoc doc;
    if (!p.Parse(doc)) {
        if (errorMsg) *errorMsg = p.errMsg;
        if (errorLine) *errorLine = p.errLine;
        return std::nullopt;
    }
    return doc;
}

} // namespace ark
