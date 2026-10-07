#include "logging.h"

#include <iostream>
#include <sstream>

#include "redaction.h"

namespace fstutorial {
namespace {

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::info: return "INFO";
        case LogLevel::warning: return "WARN";
        case LogLevel::error: return "ERROR";
    }
    return "ERROR";
}

}  // namespace

std::string format_log_message(LogLevel level,
                               const std::string& category,
                               const std::string& message,
                               const std::string& correlation_source) {
    std::ostringstream stream;
    stream << '[' << level_name(level) << "] [" << redact_sensitive(category) << ']';
    if (!correlation_source.empty()) {
        stream << " [" << safe_correlation_id(correlation_source) << ']';
    }
    stream << ' ' << redact_sensitive(message);
    return stream.str();
}

void log_message(LogLevel level,
                 const std::string& category,
                 const std::string& message,
                 const std::string& correlation_source) {
    std::clog << format_log_message(level, category, message, correlation_source) << '\n';
}

}  // namespace fstutorial
