#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <condition_variable>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fstutorial {

enum class EslConnectionState {
    disconnected,
    connecting,
    authenticating,
    subscribed,
    degraded,
};

class EslConnectionStateMachine {
public:
    EslConnectionState state() const;
    bool transition(EslConnectionState next);
    void connected();
    void failed(bool authentication_failure);
    void reset_backoff();
    std::chrono::milliseconds next_retry_delay();
    const std::string& failure_detail() const;

private:
    EslConnectionState state_{EslConnectionState::disconnected};
    std::size_t reconnect_attempt_{0};
    std::string failure_detail_;
};

struct RawEslEvent {
    std::string event_name;
    std::map<std::string, std::string> headers;
};

struct NormalizedEvent {
    std::string timestamp;
    std::string type;
    std::string correlation_id;
    std::string summary;
    std::map<std::string, std::string> fields;
};

NormalizedEvent normalize_event(const RawEslEvent& event);

class RecentEventRing {
public:
    explicit RecentEventRing(std::size_t limit);
    void push(NormalizedEvent event);
    std::vector<NormalizedEvent> snapshot() const;
    std::uint64_t dropped_count() const;

private:
    const std::size_t limit_;
    mutable std::mutex mutex_;
    std::deque<NormalizedEvent> events_;
    std::uint64_t dropped_count_{0};
};

class EventDeliveryQueue {
public:
    explicit EventDeliveryQueue(std::size_t limit);
    void push(NormalizedEvent event);
    std::vector<NormalizedEvent> drain();
    bool empty() const;
    std::uint64_t dropped_count() const;

private:
    const std::size_t limit_;
    mutable std::mutex mutex_;
    std::deque<NormalizedEvent> events_;
    std::uint64_t dropped_count_{0};
};

struct EventSubscription {
    std::uint64_t id{0};
    std::vector<NormalizedEvent> initial;
};

class EventHub {
public:
    EventHub(std::size_t recent_limit, std::size_t client_queue_limit);
    EventSubscription subscribe();
    void unsubscribe(std::uint64_t id);
    std::vector<NormalizedEvent> drain(std::uint64_t id);
    std::vector<NormalizedEvent> wait_for_events(
        std::uint64_t id, std::chrono::milliseconds timeout);
    std::uint64_t dropped_count(std::uint64_t id) const;
    std::uint64_t dropped_count() const;

    void publish(RawEslEvent event);

private:
    const std::size_t client_queue_limit_;
    mutable std::mutex mutex_;
    std::uint64_t next_id_{1};
    std::map<std::uint64_t, std::unique_ptr<EventDeliveryQueue>> clients_;
    RecentEventRing recent_;
    std::condition_variable condition_;
};

enum class EslCommand {
    status,
    sofia_status,
    tutorial_metrics,
};

std::optional<std::string> allowlisted_command(EslCommand command);
std::optional<EslCommand> parse_command_name(const std::string& name);

class SerializedCommandConnection {
public:
    using Executor = std::function<std::optional<std::string>(const std::string&)>;

    explicit SerializedCommandConnection(Executor executor = {});
    void set_executor(Executor executor);
    std::optional<std::string> execute(const std::string& name);

private:
    mutable std::mutex mutex_;
    Executor executor_;
};

}  // namespace fstutorial
