#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include "esl_observer.h"
#include "health.h"

namespace fstutorial {

struct MetricsSnapshot {
    bool freeswitch_up{false};
    bool esl_connected{false};
    std::uint64_t sessions_current{0};
    std::uint64_t sessions_total{0};
    std::uint64_t sessions_capacity{0};
    bool sessions_current_known{false};
    bool sessions_total_known{false};
    bool sessions_capacity_known{false};
    std::map<std::string, std::uint64_t> calls_total;
    std::map<std::string, double> call_duration_seconds;
    std::map<std::pair<std::string, std::string>, std::uint64_t> ivr_choice_total;
    double heartbeat_age_seconds{0};
    bool heartbeat_known{false};
    bool stale{true};
};

class MetricsRegistry {
public:
    MetricsRegistry(
        HealthRegistry& health,
        std::size_t correlation_limit,
        std::map<std::string, std::set<std::string>> ivr_allowlist = {});

    void set_esl_connected(bool connected);
    void record_status(const std::string& reply);
    void record_event(const RawEslEvent& event);
    void record_call_created(const std::string& correlation);
    void record_call_answered(const std::string& correlation);
    void record_call_hangup(const std::string& correlation, const std::string& cause);
    void record_call_destroyed(const std::string& correlation);
    void record_cdr(
        const std::string& correlation, const std::string& result, double duration_seconds);
    void record_ivr_choice(const std::string& menu, const std::string& choice);

    MetricsSnapshot snapshot() const;
    std::string prometheus() const;

private:
    struct CallRecord {
        std::chrono::system_clock::time_point created;
        bool created_observed{false};
        bool answered{false};
        bool finalized{false};
    };

    std::string bounded_result(const std::string& cause, bool answered) const;
    std::pair<std::string, std::string> bounded_ivr_choice(
        const std::string& menu, const std::string& choice) const;
    void prune_correlations();
    void update_health_locked();

    HealthRegistry& health_;
    const std::size_t correlation_limit_;
    const std::map<std::string, std::set<std::string>> ivr_allowlist_;
    mutable std::mutex mutex_;
    std::map<std::string, CallRecord> calls_;
    std::deque<std::string> correlation_order_;
    std::uint64_t sessions_current_{0};
    std::uint64_t sessions_total_{0};
    bool freeswitch_up_{false};
    bool esl_connected_{false};
    bool sessions_current_known_{false};
    bool sessions_total_known_{false};
    std::uint64_t sessions_capacity_{0};
    bool sessions_capacity_known_{false};
    std::chrono::system_clock::time_point last_heartbeat_{};
    bool heartbeat_data_degraded_{false};
    std::map<std::string, std::uint64_t> calls_total_;
    std::map<std::string, double> call_duration_seconds_;
    std::map<std::pair<std::string, std::string>, std::uint64_t> ivr_choice_total_;
};

}  // namespace fstutorial
