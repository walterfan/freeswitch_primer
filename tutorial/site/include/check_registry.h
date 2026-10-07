#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace fstutorial {

enum class CheckState { pass, fail, unavailable };

struct CheckResult {
    CheckState state{CheckState::unavailable};
    std::string evidence;
    std::string remediation;
};

using CheckFunction = std::function<CheckResult()>;

class CheckRegistry {
public:
    bool add(std::string lesson_id, std::string check_id, CheckFunction function);
    std::optional<CheckResult> run(const std::string& lesson_id,
                                   const std::string& check_id) const;
    static bool valid_identifier(const std::string& value);

private:
    static std::string key(const std::string& lesson_id, const std::string& check_id);
    mutable std::mutex mutex_;
    std::map<std::string, CheckFunction> checks_;
};

const char* check_state_name(CheckState state);

}  // namespace fstutorial

