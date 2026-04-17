// ConsoleDebugListener.h — Colored console output, Warning+ to stderr
#pragma once

#include "engine/debug/IDebugListener.h"
#include <spdlog/logger.h>
#include <memory>

namespace ark {

class ConsoleDebugListener : public IDebugListener {
public:
    ConsoleDebugListener();
    void OnDebugMessage(const LogMessage& msg) override;

private:
    std::shared_ptr<spdlog::logger> stdoutLogger_;
    std::shared_ptr<spdlog::logger> stderrLogger_;
};

} // namespace ark
