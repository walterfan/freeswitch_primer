#pragma once

#include <string>

namespace fstutorial {

std::string safe_correlation_id(const std::string& value);
std::string redact_sensitive(std::string value);

}  // namespace fstutorial

