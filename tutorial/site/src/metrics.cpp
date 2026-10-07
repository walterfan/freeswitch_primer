#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fstutorial {
namespace {

constexpr const char* CALL_RESULTS[] = {
    "answered", "busy", "no_answer", "rejected", "failed"};

std::string header_value(const RawEslEvent& event, const char* name) {
    const auto found = event.headers.find(name);
    return found == event.headers.end() ? std::string{} : found->second;
}

bool parse_counter(const std::string& value, std::uint64_t& output) {
    if (value.empty()) return false;
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) return false;
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_duration(const std::string& value, double& output) {
    if (value.empty()) return false;
    try {
        std::size_t consumed = 0;
        output = std::stod(value, &consumed);
        return consumed == value.size() && std::isfinite(output) && output >= 0;
    } catch (...) {
        return false;
    }
}

bool extract_counter(
    const std::string& text, const std::regex& pattern, std::uint64_t& output) {
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 2) return false;
    return parse_counter(match[1].str(), output);
}

void append_help(std::ostringstream& output, const char* name, const char* type) {
    output << "# HELP " << name << " FreeSWITCH tutorial " << name << "\n";
    output << "# TYPE " << name << ' ' << type << "\n";
}

}  // namespace

MetricsRegistry::MetricsRegistry(
    HealthRegistry& health,
    std::size_t correlation_limit,
    std::map<std::string, std::set<std::string>> ivr_allowlist)
    : health_(health),
      correlation_limit_(correlation_limit),
      ivr_allowlist_(std::move(ivr_allowlist)) {
    if (correlation_limit_ == 0) throw std::invalid_argument("correlation limit must be positive");
    for (const auto* result : CALL_RESULTS) {
        calls_total_.emplace(result, 0);
        call_duration_seconds_.emplace(result, 0);
    }
}

void MetricsRegistry::set_esl_connected(bool connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    esl_connected_ = connected;
    if (!connected) freeswitch_up_ = false;
    update_health_locked();
}

void MetricsRegistry::record_status(const std::string& reply) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool up = reply.rfind("UP", 0) == 0;
    freeswitch_up_ = up;
    std::uint64_t capacity = 0;
    if (extract_counter(
            reply,
            std::regex{"(?:Max-Sessions|max sessions|capacity)\\D+(\\d+)",
                       std::regex::icase},
            capacity)) {
        sessions_capacity_ = capacity;
        sessions_capacity_known_ = true;
    }
    update_health_locked();
    if (!up) {
        health_.update("freeswitch", {
            ComponentState::degraded,
            "FreeSWITCH status is not UP",
            "inspect the status command and FreeSWITCH logs",
            std::chrono::system_clock::now(),
        });
    }
}

std::string MetricsRegistry::bounded_result(const std::string& cause, bool answered) const {
    if (cause == "answered" || cause == "busy" || cause == "no_answer" ||
        cause == "rejected" || cause == "failed") {
        return cause;
    }
    if (answered) return "answered";
    if (cause.find("USER_BUSY") != std::string::npos ||
        cause.find("CALL_REJECTED") != std::string::npos) {
        return cause.find("USER_BUSY") != std::string::npos ? "busy" : "rejected";
    }
    if (cause.find("NO_ANSWER") != std::string::npos ||
        cause.find("NO_USER_RESPONSE") != std::string::npos) {
        return "no_answer";
    }
    return "failed";
}

std::pair<std::string, std::string> MetricsRegistry::bounded_ivr_choice(
    const std::string& menu, const std::string& choice) const {
    const auto menu_found = ivr_allowlist_.find(menu);
    if (menu_found == ivr_allowlist_.end()) return {"other", "other"};
    return menu_found->second.count(choice) ? std::make_pair(menu, choice)
                                            : std::make_pair(menu, "other");
}

void MetricsRegistry::prune_correlations() {
    while (calls_.size() > correlation_limit_ && !correlation_order_.empty()) {
        const auto oldest = correlation_order_.front();
        correlation_order_.pop_front();
        const auto found = calls_.find(oldest);
        if (found == calls_.end()) continue;
        if (found->second.created_observed && !found->second.finalized &&
            sessions_current_ > 0) {
            --sessions_current_;
        }
        calls_.erase(found);
    }
}

void MetricsRegistry::record_call_created(const std::string& correlation) {
    if (correlation.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto [found, inserted] = calls_.try_emplace(correlation);
    if (inserted) {
        found->second.created = std::chrono::system_clock::now();
        correlation_order_.push_back(correlation);
    }
    if (!found->second.created_observed) {
        found->second.created_observed = true;
        ++sessions_current_;
        ++sessions_total_;
    }
    freeswitch_up_ = true;
    update_health_locked();
    prune_correlations();
}

void MetricsRegistry::record_call_answered(const std::string& correlation) {
    if (correlation.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto [found, inserted] = calls_.try_emplace(correlation);
    if (inserted) {
        found->second.created = std::chrono::system_clock::now();
        correlation_order_.push_back(correlation);
    }
    found->second.answered = true;
    update_health_locked();
    prune_correlations();
}

void MetricsRegistry::record_call_hangup(
    const std::string& correlation, const std::string& cause) {
    if (correlation.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto [found, inserted] = calls_.try_emplace(correlation);
    if (inserted) {
        found->second.created = std::chrono::system_clock::now();
        correlation_order_.push_back(correlation);
    }
    if (found->second.finalized) return;
    found->second.finalized = true;
    const auto result = bounded_result(cause, found->second.answered);
    const auto duration = std::chrono::duration<double>(
        std::chrono::system_clock::now() - found->second.created).count();
    ++calls_total_[result];
    call_duration_seconds_[result] += std::max(0.0, duration);
    update_health_locked();
    prune_correlations();
}

void MetricsRegistry::record_call_destroyed(const std::string& correlation) {
    if (correlation.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = calls_.find(correlation);
    if (found == calls_.end() || !found->second.created_observed) return;
    if (sessions_current_ > 0) --sessions_current_;
    found->second.created_observed = false;
    update_health_locked();
}

void MetricsRegistry::record_cdr(
    const std::string& correlation, const std::string& result, double duration_seconds) {
    if (correlation.empty() || !std::isfinite(duration_seconds) || duration_seconds < 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto [found, inserted] = calls_.try_emplace(correlation);
    if (inserted) {
        found->second.created = std::chrono::system_clock::now();
        correlation_order_.push_back(correlation);
    }
    if (found->second.finalized) return;
    found->second.finalized = true;
    const auto bounded = bounded_result(result, result == "answered");
    ++calls_total_[bounded];
    call_duration_seconds_[bounded] += duration_seconds;
    update_health_locked();
    prune_correlations();
}

void MetricsRegistry::record_ivr_choice(const std::string& menu, const std::string& choice) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++ivr_choice_total_[bounded_ivr_choice(menu, choice)];
    update_health_locked();
}

void MetricsRegistry::record_event(const RawEslEvent& event) {
    const auto normalized = normalize_event(event);
    if (event.event_name == "HEARTBEAT") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::uint64_t sessions = 0;
        const bool current_valid = parse_counter(header_value(event, "Session-Count"), sessions);
        if (current_valid) {
            sessions_current_ = sessions;
            sessions_current_known_ = true;
        }
        const bool total_valid = parse_counter(
            header_value(event, "Session-Since-Startup"), sessions);
        if (total_valid) {
            sessions_total_ = sessions;
            sessions_total_known_ = true;
        }
        heartbeat_data_degraded_ = !current_valid || !total_valid;
        last_heartbeat_ = std::chrono::system_clock::now();
        freeswitch_up_ = true;
        update_health_locked();
        return;
    }
    const auto& correlation = normalized.correlation_id;
    if (event.event_name == "CHANNEL_CREATE") record_call_created(correlation);
    if (event.event_name == "CHANNEL_ANSWER" || event.event_name == "CHANNEL_BRIDGE") {
        record_call_answered(correlation);
    }
    if (event.event_name == "CHANNEL_HANGUP" ||
        event.event_name == "CHANNEL_HANGUP_COMPLETE") {
        record_call_hangup(correlation, header_value(event, "Hangup-Cause"));
    }
    if (event.event_name == "CHANNEL_DESTROY") record_call_destroyed(correlation);
    if (event.event_name == "CDR") {
        double duration = 0;
        if (parse_duration(header_value(event, "Duration"), duration)) {
            record_cdr(correlation, header_value(event, "Answer-State"), duration);
        }
    }
    if (event.event_name == "CUSTOM" &&
        header_value(event, "Event-Subclass") == "tutorial::ivr_choice") {
        record_ivr_choice(header_value(event, "Menu"), header_value(event, "Choice"));
    }
}

void MetricsRegistry::update_health_locked() {
    const auto now = std::chrono::system_clock::now();
    health_.update("metrics", {
        heartbeat_data_degraded_ ? ComponentState::degraded : ComponentState::healthy,
        heartbeat_data_degraded_
            ? "Metrics snapshot has malformed HEARTBEAT session fields"
            : "Metrics snapshot is available",
        heartbeat_data_degraded_ ? "inspect HEARTBEAT headers before using session gauges" : "",
        now,
    });
    if (last_heartbeat_ != std::chrono::system_clock::time_point{}) {
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_heartbeat_).count();
        health_.update("heartbeat", {
            age > 15
                ? ComponentState::stale
                : heartbeat_data_degraded_ ? ComponentState::degraded : ComponentState::healthy,
            age > 15
                ? "HEARTBEAT is stale"
                : heartbeat_data_degraded_
                    ? "HEARTBEAT session fields are malformed"
                    : "HEARTBEAT is fresh",
            age > 15
                ? "check the ESL event connection"
                : heartbeat_data_degraded_
                    ? "check the FreeSWITCH heartbeat format"
                    : "",
            last_heartbeat_,
        });
    }
}

MetricsSnapshot MetricsRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MetricsSnapshot output;
    output.freeswitch_up = freeswitch_up_ && esl_connected_;
    output.esl_connected = esl_connected_;
    output.sessions_current = sessions_current_;
    output.sessions_total = sessions_total_;
    output.sessions_capacity = sessions_capacity_;
    output.sessions_current_known = sessions_current_known_;
    output.sessions_total_known = sessions_total_known_;
    output.sessions_capacity_known = sessions_capacity_known_;
    output.calls_total = calls_total_;
    output.call_duration_seconds = call_duration_seconds_;
    output.ivr_choice_total = ivr_choice_total_;
    output.heartbeat_known = last_heartbeat_ != std::chrono::system_clock::time_point{};
    if (output.heartbeat_known) {
        output.heartbeat_age_seconds = std::chrono::duration<double>(
            std::chrono::system_clock::now() - last_heartbeat_).count();
    } else {
        output.heartbeat_age_seconds = std::numeric_limits<double>::quiet_NaN();
    }
    output.stale = !output.heartbeat_known || output.heartbeat_age_seconds > 15;
    return output;
}

std::string MetricsRegistry::prometheus() const {
    const auto metrics = snapshot();
    std::ostringstream output;
    append_help(output, "freeswitch_up", "gauge");
    output << "freeswitch_up " << (metrics.freeswitch_up ? 1 : 0) << "\n";
    append_help(output, "freeswitch_esl_connected", "gauge");
    output << "freeswitch_esl_connected " << (metrics.esl_connected ? 1 : 0) << "\n";
    append_help(output, "freeswitch_sessions_current", "gauge");
    output << "freeswitch_sessions_current "
           << (metrics.sessions_current_known ? std::to_string(metrics.sessions_current) : "NaN")
           << "\n";
    append_help(output, "freeswitch_sessions_total", "counter");
    output << "freeswitch_sessions_total "
           << (metrics.sessions_total_known ? std::to_string(metrics.sessions_total) : "NaN")
           << "\n";
    append_help(output, "freeswitch_sessions_capacity", "gauge");
    output << "freeswitch_sessions_capacity "
           << (metrics.sessions_capacity_known
                   ? std::to_string(metrics.sessions_capacity)
                   : "NaN")
           << "\n";
    append_help(output, "freeswitch_calls_total", "counter");
    for (const auto* result : CALL_RESULTS) {
        output << "freeswitch_calls_total{result=\"" << result << "\"} "
               << metrics.calls_total.at(result) << "\n";
    }
    append_help(output, "freeswitch_call_duration_seconds", "counter");
    output << std::fixed << std::setprecision(3);
    for (const auto* result : CALL_RESULTS) {
        output << "freeswitch_call_duration_seconds{result=\"" << result << "\"} "
               << metrics.call_duration_seconds.at(result) << "\n";
    }
    append_help(output, "freeswitch_heartbeat_age_seconds", "gauge");
    if (std::isnan(metrics.heartbeat_age_seconds)) {
        output << "freeswitch_heartbeat_age_seconds NaN\n";
    } else {
        output << "freeswitch_heartbeat_age_seconds " << metrics.heartbeat_age_seconds << "\n";
    }
    append_help(output, "freeswitch_ivr_choice_total", "counter");
    if (metrics.ivr_choice_total.empty()) {
        output << "freeswitch_ivr_choice_total{menu=\"other\",choice=\"other\"} 0\n";
    } else {
        for (const auto& [key, count] : metrics.ivr_choice_total) {
            output << "freeswitch_ivr_choice_total{menu=\"" << key.first
                   << "\",choice=\"" << key.second << "\"} " << count << "\n";
        }
    }
    return output.str();
}

}  // namespace fstutorial
