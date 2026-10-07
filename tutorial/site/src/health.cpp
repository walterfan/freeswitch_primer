#include "health.h"

#include <mutex>
#include <utility>

namespace fstutorial {

HealthRegistry::HealthRegistry() {
    components_.emplace("service", ComponentHealth{
        ComponentState::healthy, "teaching service is running", "", std::chrono::system_clock::now()});
    components_.emplace("content", ComponentHealth{
        ComponentState::healthy, "lesson content is available", "", std::chrono::system_clock::now()});
    for (const char* name : {"freeswitch", "esl", "heartbeat", "sip_profile", "metrics"}) {
        components_.emplace(name, ComponentHealth{
            ComponentState::unavailable,
            "runtime observation has not started",
            "check FreeSWITCH and ESL configuration",
            {}});
    }
}

void HealthRegistry::update(const std::string& component, ComponentHealth health) {
    std::lock_guard<std::mutex> lock(mutex_);
    components_[component] = std::move(health);
}

std::map<std::string, ComponentHealth> HealthRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return components_;
}

ComponentState HealthRegistry::overall_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ComponentState overall = ComponentState::healthy;
    for (const auto& [_, health] : components_) {
        if (health.state == ComponentState::unavailable) return ComponentState::unavailable;
        if (health.state == ComponentState::stale) overall = ComponentState::stale;
        if (health.state == ComponentState::degraded && overall == ComponentState::healthy) {
            overall = ComponentState::degraded;
        }
    }
    return overall;
}

const char* component_state_name(ComponentState state) {
    switch (state) {
        case ComponentState::healthy: return "healthy";
        case ComponentState::degraded: return "degraded";
        case ComponentState::stale: return "stale";
        case ComponentState::unavailable: return "unavailable";
    }
    return "unavailable";
}

}  // namespace fstutorial
