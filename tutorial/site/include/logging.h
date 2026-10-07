#pragma once

#include <string>

namespace fstutorial {

enum class LogLevel { info, warning, error };

std::string format_log_message(LogLevel level,
                               const std::string& category,
                               const std::string& message,
                               const std::string& correlation_source = {});
void log_message(LogLevel level,
                 const std::string& category,
                 const std::string& message,
                 const std::string& correlation_source = {});

}  // namespace fstutorial
