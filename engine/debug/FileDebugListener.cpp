// FileDebugListener.cpp — Rotating file sink (10MB, 5 files)
#include "engine/debug/FileDebugListener.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>

namespace ark {

FileDebugListener::FileDebugListener(const std::string& logDir) {
    std::filesystem::create_directories(logDir);
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logDir + "StarArk.log", 10 * 1024 * 1024, 5);
    logger_ = std::make_shared<spdlog::logger>("ark_file", sink);
    logger_->set_pattern("%v");
    logger_->set_level(spdlog::level::trace);
}

void FileDebugListener::OnDebugMessage(const LogMessage& msg) {
    std::string_view file = msg.location.file_name();
    auto pos = file.find_last_of("/\\");
    if (pos != std::string_view::npos) file = file.substr(pos + 1);

    std::string formatted = "[" + msg.timestamp + "] [" + LogLevelToString(msg.level) + "] [" +
                            msg.category + "] " + msg.message + " (" +
                            std::string(file) + ":" + std::to_string(msg.location.line()) + ")";

    logger_->info(formatted);
    logger_->flush();
}

} // namespace ark
