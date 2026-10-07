#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace fstutorial {

enum class ComponentState { healthy, degraded, stale, unavailable };

struct ComponentHealth {
    ComponentState state{ComponentState::unavailable};
    std::string detail;
    std::string remediation;
    std::chrono::system_clock::time_point last_success{};
};

class HealthRegistry {
public:
    HealthRegistry();
    void update(const std::string& component, ComponentHealth health);
    std::map<std::string, ComponentHealth> snapshot() const;
    ComponentState overall_state() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, ComponentHealth> components_;
};

const char* component_state_name(ComponentState state);

}  // namespace fstutorial
