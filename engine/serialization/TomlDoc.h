#pragma once

// -----------------------------------------------------------------------------
// TomlDoc — v0.2 15.C 自写 TOML 子集
//
// 支持范围（覆盖场景 TOML 所需的全部语法，不追求通用性）:
//   - 标量: bool / int64 / double / string（双引号）
//   - 数组: 同质或异质均可，如 [1.0, 2.0, 3.0] / ["a", "b"]
//   - 子表: [table.path]
//   - 数组表: [[objects]]，多次出现 = 追加一个元素
//   - 注释: # 行尾
//   - 未支持: 多行字符串 / 日期 / 内联表 / 字面量字符串
//
// 决定自写而非引入 toml++ 的原因: 工程 FetchContent 全部指向本地 deps_cache/*.zip，
// 不具备拉第三方的路径；v0.1 SceneSerializer 已有手写 JSON 解析先例，保持风格一致。
//
// 使用:
//   TomlDoc doc;
//   auto& scene = doc.Root().GetOrCreateTable("scene");
//   scene.Set("name", TomlValue::String("Cottage"));
//   auto& objs = doc.Root().GetOrCreateArrayOfTables("objects");
//   objs.Append().Set("guid", TomlValue::String("..."));
//   std::string text = doc.Dump();
//
//   auto parsed = TomlDoc::Parse(text);  // std::optional<TomlDoc>
//   if (parsed) { ... }
// -----------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ark {

class TomlTable;
class TomlValue;

using TomlArray = std::vector<TomlValue>;

class TomlValue {
public:
    enum class Kind { Null, Bool, Int, Float, String, Array, Table };

    TomlValue() = default;
    TomlValue(const TomlValue& other);
    TomlValue(TomlValue&& other) noexcept = default;
    TomlValue& operator=(const TomlValue& other);
    TomlValue& operator=(TomlValue&& other) noexcept = default;

    static TomlValue Bool(bool b);
    static TomlValue Int(std::int64_t i);
    static TomlValue Float(double d);
    static TomlValue String(std::string s);
    static TomlValue Array(TomlArray arr);
    static TomlValue Table(TomlTable t);

    Kind GetKind() const { return kind_; }
    bool IsBool()   const { return kind_ == Kind::Bool; }
    bool IsInt()    const { return kind_ == Kind::Int; }
    bool IsFloat()  const { return kind_ == Kind::Float; }
    bool IsNumber() const { return kind_ == Kind::Int || kind_ == Kind::Float; }
    bool IsString() const { return kind_ == Kind::String; }
    bool IsArray()  const { return kind_ == Kind::Array; }
    bool IsTable()  const { return kind_ == Kind::Table; }

    bool               AsBool()   const { return b_; }
    std::int64_t       AsInt()    const { return i_; }
    double             AsFloat()  const { return kind_ == Kind::Int ? static_cast<double>(i_) : f_; }
    const std::string& AsString() const { return s_; }
    const TomlArray&   AsArray()  const { return arr_; }
    TomlArray&         AsArray()        { return arr_; }
    const TomlTable&   AsTable()  const;
    TomlTable&         AsTable();

private:
    Kind         kind_ = Kind::Null;
    bool         b_ = false;
    std::int64_t i_ = 0;
    double       f_ = 0.0;
    std::string  s_;
    TomlArray    arr_;
    // TomlTable 用 unique_ptr 避免与自身的不完整类型循环
    std::unique_ptr<TomlTable> table_;
};

class TomlTable {
public:
    TomlTable() = default;
    TomlTable(const TomlTable& other);
    TomlTable(TomlTable&& other) noexcept = default;
    TomlTable& operator=(const TomlTable& other);
    TomlTable& operator=(TomlTable&& other) noexcept = default;

    // --- scalar + array-of-scalar fields ---
    void Set(std::string_view key, TomlValue value);
    const TomlValue* Find(std::string_view key) const;
    TomlValue*       Find(std::string_view key);

    // --- 子表 ---
    TomlTable& GetOrCreateTable(std::string_view key);
    const TomlTable* FindTable(std::string_view key) const;

    // --- 数组表（[[foo]]） ---
    class ArrayOfTables {
    public:
        TomlTable& Append();
        std::size_t Size() const { return tables_.size(); }
        TomlTable&       operator[](std::size_t i)       { return *tables_[i]; }
        const TomlTable& operator[](std::size_t i) const { return *tables_[i]; }
    private:
        std::vector<std::unique_ptr<TomlTable>> tables_;
        friend class TomlDocWriter;
        friend class TomlTable;
    };

    ArrayOfTables& GetOrCreateArrayOfTables(std::string_view key);
    const ArrayOfTables* FindArrayOfTables(std::string_view key) const;
    ArrayOfTables*       FindArrayOfTables(std::string_view key);

    // --- 迭代（按插入顺序） ---
    const std::vector<std::string>& Keys()       const { return keyOrder_; }
    const std::vector<std::string>& TableKeys()  const { return tableOrder_; }
    const std::vector<std::string>& AotKeys()    const { return aotOrder_; }

    const TomlTable* GetTableByName(std::string_view key) const;

private:
    // 平坦 key -> scalar/array
    std::map<std::string, TomlValue, std::less<>> values_;
    std::vector<std::string> keyOrder_;

    // 子表
    std::map<std::string, std::unique_ptr<TomlTable>, std::less<>> tables_;
    std::vector<std::string> tableOrder_;

    // 数组表
    std::map<std::string, std::unique_ptr<ArrayOfTables>, std::less<>> aots_;
    std::vector<std::string> aotOrder_;
};

class TomlDoc {
public:
    TomlDoc() = default;

    TomlTable& Root() { return root_; }
    const TomlTable& Root() const { return root_; }

    // 序列化为文本（稳定输出顺序：按插入顺序）
    std::string Dump() const;

    // 解析。失败返回 std::nullopt；errorLine / errorMsg 可选带出。
    static std::optional<TomlDoc> Parse(std::string_view text,
                                         std::string* errorMsg = nullptr,
                                         int* errorLine = nullptr);

private:
    TomlTable root_;
};

} // namespace ark
