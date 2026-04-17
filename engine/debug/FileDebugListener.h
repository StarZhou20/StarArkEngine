// FileDebugListener.h — Writes all log messages to rotating log files via spdlog
#pragma once

#include "engine/debug/IDebugListener.h"
#include <spdlog/logger.h>
#include <memory>
#include <string>

namespace ark {

class FileDebugListener : public IDebugListener {
public:
    explicit FileDebugListener(const std::string& logDir = "logs/");
    void OnDebugMessage(const LogMessage& msg) override;

private:
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace ark
