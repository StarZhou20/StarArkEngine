// DebugListenBus.cpp — Singleton bus, timestamp generation, broadcast to all listeners
#include "engine/debug/DebugListenBus.h"
#include "engine/debug/IDebugListener.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace ark {

DebugListenBus& DebugListenBus::Get() {
    static DebugListenBus instance;
    return instance;
}

void DebugListenBus::RegisterListener(IDebugListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(listener);
}

void DebugListenBus::UnregisterListener(IDebugListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void DebugListenBus::Broadcast(LogLevel level, const std::string& category, const std::string& message,
                                std::source_location location) {
    // Generate ISO 8601 timestamp with milliseconds
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();

    LogMessage msg{level, category, message, oss.str(), location};

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto* listener : listeners_) {
        listener->OnDebugMessage(msg);
    }
}

} // namespace ark
