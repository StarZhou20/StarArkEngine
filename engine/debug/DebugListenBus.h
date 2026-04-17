// DebugListenBus.h — Global debug message bus, LogLevel, LogMessage, ARK_LOG macros
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <source_location>
#include <cstdlib>

namespace ark {

class IDebugListener;

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

inline const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

struct LogMessage {
    LogLevel level;
    std::string category;
    std::string message;
    std::string timestamp;
    std::source_location location;
};

class DebugListenBus {
public:
    static DebugListenBus& Get();

    void RegisterListener(IDebugListener* listener);
    void UnregisterListener(IDebugListener* listener);
    void Broadcast(LogLevel level, const std::string& category, const std::string& message,
                   std::source_location location = std::source_location::current());

    DebugListenBus(const DebugListenBus&) = delete;
    DebugListenBus& operator=(const DebugListenBus&) = delete;

private:
    DebugListenBus() = default;
    std::vector<IDebugListener*> listeners_;
    std::mutex mutex_;
};

} // namespace ark

// Convenience macros — source_location captured at call site via default argument
#define ARK_LOG_TRACE(cat, msg)  ::ark::DebugListenBus::Get().Broadcast(::ark::LogLevel::Trace, cat, msg)
#define ARK_LOG_DEBUG(cat, msg)  ::ark::DebugListenBus::Get().Broadcast(::ark::LogLevel::Debug, cat, msg)
#define ARK_LOG_INFO(cat, msg)   ::ark::DebugListenBus::Get().Broadcast(::ark::LogLevel::Info, cat, msg)
#define ARK_LOG_WARN(cat, msg)   ::ark::DebugListenBus::Get().Broadcast(::ark::LogLevel::Warning, cat, msg)
#define ARK_LOG_ERROR(cat, msg)  ::ark::DebugListenBus::Get().Broadcast(::ark::LogLevel::Error, cat, msg)
#define ARK_LOG_FATAL(cat, msg)  do { ::ark::DebugListenBus::Get().Broadcast(::ark::LogLevel::Fatal, cat, msg); std::abort(); } while(0)
