#include "check_registry.h"

#include <regex>

namespace fstutorial {

bool CheckRegistry::valid_identifier(const std::string& value) {
    static const std::regex pattern{"^(?:day-[0-9]{2}|[a-z0-9]+(?:-[a-z0-9]+)*)$"};
    return std::regex_match(value, pattern);
}

std::string CheckRegistry::key(const std::string& lesson_id, const std::string& check_id) {
    return lesson_id + "/" + check_id;
}

bool CheckRegistry::add(std::string lesson_id, std::string check_id, CheckFunction function) {
    if (!valid_identifier(lesson_id) || !valid_identifier(check_id) || !function) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return checks_.emplace(key(lesson_id, check_id), std::move(function)).second;
}

std::optional<CheckResult> CheckRegistry::run(const std::string& lesson_id,
                                               const std::string& check_id) const {
    if (!valid_identifier(lesson_id) || !valid_identifier(check_id)) return std::nullopt;
    CheckFunction function;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = checks_.find(key(lesson_id, check_id));
        if (found == checks_.end()) return std::nullopt;
        function = found->second;
    }
    return function();
}

const char* check_state_name(CheckState state) {
    switch (state) {
        case CheckState::pass: return "pass";
        case CheckState::fail: return "fail";
        case CheckState::unavailable: return "unavailable";
    }
    return "unavailable";
}

}  // namespace fstutorial

