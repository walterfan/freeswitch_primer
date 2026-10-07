#include "esl_observer.h"

#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fstutorial {
namespace {

bool valid_transition(EslConnectionState from, EslConnectionState to) {
    switch (from) {
        case EslConnectionState::disconnected:
            return to == EslConnectionState::connecting;
        case EslConnectionState::connecting:
            return to == EslConnectionState::authenticating ||
                   to == EslConnectionState::degraded ||
                   to == EslConnectionState::disconnected;
        case EslConnectionState::authenticating:
            return to == EslConnectionState::subscribed ||
                   to == EslConnectionState::degraded ||
                   to == EslConnectionState::disconnected;
        case EslConnectionState::subscribed:
            return to == EslConnectionState::degraded ||
                   to == EslConnectionState::disconnected;
        case EslConnectionState::degraded:
            return to == EslConnectionState::connecting ||
                   to == EslConnectionState::disconnected;
    }
    return false;
}

bool safe_event_value(const std::string& value) {
    static const std::regex pattern{"^[A-Za-z0-9_.:-]{1,64}$"};
    return std::regex_match(value, pattern);
}

std::string correlation_id(const std::string& uuid) {
    if (uuid.empty()) return {};
    std::ostringstream output;
    output << "corr-" << std::hex << std::setw(16) << std::setfill('0')
           << std::hash<std::string>{}(uuid);
    return output.str();
}

}  // namespace

EslConnectionState EslConnectionStateMachine::state() const {
    return state_;
}

bool EslConnectionStateMachine::transition(EslConnectionState next) {
    if (!valid_transition(state_, next)) return false;
    state_ = next;
    return true;
}

void EslConnectionStateMachine::connected() {
    state_ = EslConnectionState::subscribed;
    reconnect_attempt_ = 0;
    failure_detail_.clear();
}

void EslConnectionStateMachine::failed(bool authentication_failure) {
    state_ = EslConnectionState::degraded;
    failure_detail_ = authentication_failure
        ? "ESL authentication failed"
        : "ESL connection unavailable";
}

void EslConnectionStateMachine::reset_backoff() {
    reconnect_attempt_ = 0;
}

std::chrono::milliseconds EslConnectionStateMachine::next_retry_delay() {
    const std::size_t shift = reconnect_attempt_ > 7 ? 7 : reconnect_attempt_;
    const auto delay = std::chrono::milliseconds{250u << shift};
    ++reconnect_attempt_;
    return delay > std::chrono::seconds{30} ? std::chrono::seconds{30} : delay;
}

const std::string& EslConnectionStateMachine::failure_detail() const {
    return failure_detail_;
}

NormalizedEvent normalize_event(const RawEslEvent& event) {
    NormalizedEvent normalized;
    normalized.timestamp = event.headers.count("Event-Date-Timestamp")
        ? event.headers.at("Event-Date-Timestamp")
        : event.headers.count("Event-Date-Local")
            ? event.headers.at("Event-Date-Local")
            : "";
    normalized.type = safe_event_value(event.event_name) ? event.event_name : "UNKNOWN";

    for (const char* name : {"Channel-State", "Channel-Call-State", "Answer-State",
                             "Hangup-Cause", "Application", "Event-Subclass",
                             "Menu", "Choice"}) {
        const auto found = event.headers.find(name);
        if (found != event.headers.end() && safe_event_value(found->second)) {
            normalized.fields.emplace(name, found->second);
        }
    }

    for (const char* name : {"Unique-ID", "Channel-Call-UUID", "variable_uuid"}) {
        const auto found = event.headers.find(name);
        if (found != event.headers.end()) {
            normalized.correlation_id = correlation_id(found->second);
            if (!normalized.correlation_id.empty()) break;
        }
    }
    const auto summary = normalized.fields.find("Answer-State");
    normalized.summary = summary != normalized.fields.end()
        ? summary->second
        : normalized.type == "UNKNOWN" ? "event observed" : normalized.type;
    return normalized;
}

RecentEventRing::RecentEventRing(std::size_t limit) : limit_(limit) {
    if (limit_ == 0) throw std::invalid_argument("recent event limit must be positive");
}

void RecentEventRing::push(NormalizedEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(event));
    if (events_.size() > limit_) {
        events_.pop_front();
        ++dropped_count_;
    }
}

std::vector<NormalizedEvent> RecentEventRing::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {events_.begin(), events_.end()};
}

std::uint64_t RecentEventRing::dropped_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_count_;
}

EventDeliveryQueue::EventDeliveryQueue(std::size_t limit) : limit_(limit) {
    if (limit_ == 0) throw std::invalid_argument("client queue limit must be positive");
}

void EventDeliveryQueue::push(NormalizedEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(event));
    if (events_.size() > limit_) {
        events_.pop_front();
        ++dropped_count_;
    }
}

std::vector<NormalizedEvent> EventDeliveryQueue::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NormalizedEvent> output{events_.begin(), events_.end()};
    events_.clear();
    return output;
}

bool EventDeliveryQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.empty();
}

std::uint64_t EventDeliveryQueue::dropped_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_count_;
}

EventHub::EventHub(std::size_t recent_limit, std::size_t client_queue_limit)
    : client_queue_limit_(client_queue_limit), recent_(recent_limit) {
    if (client_queue_limit_ == 0) {
        throw std::invalid_argument("client queue limit must be positive");
    }
}

EventSubscription EventHub::subscribe() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto id = next_id_++;
    clients_.emplace(id, std::make_unique<EventDeliveryQueue>(client_queue_limit_));
    return {id, recent_.snapshot()};
}

void EventHub::unsubscribe(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(id);
}

std::vector<NormalizedEvent> EventHub::drain(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = clients_.find(id);
    return found == clients_.end() ? std::vector<NormalizedEvent>{} : found->second->drain();
}

std::vector<NormalizedEvent> EventHub::wait_for_events(
    std::uint64_t id, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto has_events = [this, id] {
        const auto found = clients_.find(id);
        return found == clients_.end() || !found->second->empty();
    };
    condition_.wait_for(lock, timeout, has_events);
    const auto found = clients_.find(id);
    return found == clients_.end() ? std::vector<NormalizedEvent>{} : found->second->drain();
}

std::uint64_t EventHub::dropped_count(std::uint64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = clients_.find(id);
    return found == clients_.end() ? 0 : found->second->dropped_count();
}

std::uint64_t EventHub::dropped_count() const {
    return recent_.dropped_count();
}

void EventHub::publish(RawEslEvent event) {
    auto normalized = normalize_event(event);
    recent_.push(normalized);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [_, client] : clients_) client->push(normalized);
    }
    condition_.notify_all();
}

std::optional<std::string> allowlisted_command(EslCommand command) {
    switch (command) {
        case EslCommand::status: return "status";
        case EslCommand::sofia_status: return "sofia status";
        case EslCommand::tutorial_metrics: return "tutorial_metrics";
    }
    return std::nullopt;
}

std::optional<EslCommand> parse_command_name(const std::string& name) {
    if (name == "status") return EslCommand::status;
    if (name == "sofia_profile_status") return EslCommand::sofia_status;
    if (name == "tutorial_metrics") return EslCommand::tutorial_metrics;
    return std::nullopt;
}

SerializedCommandConnection::SerializedCommandConnection(Executor executor)
    : executor_(std::move(executor)) {}

void SerializedCommandConnection::set_executor(Executor executor) {
    std::lock_guard<std::mutex> lock(mutex_);
    executor_ = std::move(executor);
}

std::optional<std::string> SerializedCommandConnection::execute(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto command = parse_command_name(name);
    if (!command || !executor_) return std::nullopt;
    const auto wire_command = allowlisted_command(*command);
    return wire_command ? executor_(*wire_command) : std::nullopt;
}

}  // namespace fstutorial
