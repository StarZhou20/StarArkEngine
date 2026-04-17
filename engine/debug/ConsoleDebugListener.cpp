// ConsoleDebugListener.cpp — Colored stdout/stderr output via spdlog
#include "engine/debug/ConsoleDebugListener.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace ark {

ConsoleDebugListener::ConsoleDebugListener() {
    auto stdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdoutLogger_ = std::make_shared<spdlog::logger>("ark_stdout", stdoutSink);
    stdoutLogger_->set_pattern("%v");
    stdoutLogger_->set_level(spdlog::level::trace);

    auto stderrSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    stderrLogger_ = std::make_shared<spdlog::logger>("ark_stderr", stderrSink);
    stderrLogger_->set_pattern("%v");
    stderrLogger_->set_level(spdlog::level::trace);
}

void ConsoleDebugListener::OnDebugMessage(const LogMessage& msg) {
    std::string_view file = msg.location.file_name();
    auto pos = file.find_last_of("/\\");
    if (pos != std::string_view::npos) file = file.substr(pos + 1);

    std::string formatted = "[" + msg.timestamp + "] [" + LogLevelToString(msg.level) + "] [" +
                            msg.category + "] " + msg.message + " (" +
                            std::string(file) + ":" + std::to_string(msg.location.line()) + ")";

    if (msg.level >= LogLevel::Warning) {
        stderrLogger_->warn(formatted);
    } else {
        stdoutLogger_->info(formatted);
    }
}

} // namespace ark
