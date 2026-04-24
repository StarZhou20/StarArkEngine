#pragma once

// -----------------------------------------------------------------------------
// Guid — v0.2 15.B
//
// UUID v4（随机）文本表示。格式: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
//   - 36 字符，含 4 个连字符
//   - y 的高两位固定为 10（RFC 4122 variant）
//   - 第三段首字符固定为 '4'（version 4）
//
// 为什么是字符串而不是 128-bit POD:
//   - TOML / 日志 / Inspector / C# 互操作都以字符串为主
//   - v0.2 的 GUID 只在场景加载/保存时热路径使用；不是每帧操作
//   - 将来可再加一个 `GuidU128` 二进制形式做热路径，不冲突
//
// 线程: `NewGuid()` 使用 thread_local RNG，可多线程并发调用。
// -----------------------------------------------------------------------------

#include <string>
#include <string_view>

namespace ark {

class Guid {
public:
    // 生成新 UUID v4。
    static std::string NewGuid();

    // 校验一个字符串是否满足 UUID 文本格式（不强求 v4，仅形状）。
    static bool IsValid(std::string_view s);
};

} // namespace ark
