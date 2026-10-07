#include "esl_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#include <esl.h>

namespace fstutorial {
namespace {

std::string header_value(esl_event_t* event, const char* name) {
    const char* value = esl_event_get_header(event, name);
    return value == nullptr ? std::string{} : std::string{value};
}

bool authentication_failure(const esl_handle_t& handle) {
    std::string reply{handle.last_reply};
    std::transform(reply.begin(), reply.end(), reply.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return reply.find("auth") != std::string::npos ||
           reply.find("password") != std::string::npos;
}

}  // namespace

struct EslEventClient::Impl {
    Impl(
        EslConfig config_value,
        HealthRegistry& health_value,
        EventHandler handler_value,
        StateHandler state_handler_value)
        : config(std::move(config_value)),
          health(health_value),
          handler(std::move(handler_value)),
          state_handler(std::move(state_handler_value)) {}

    EslConfig config;
    HealthRegistry& health;
    EventHandler handler;
    StateHandler state_handler;
    EslConnectionStateMachine state_machine;
    esl_handle_t handle{};
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::mutex state_mutex;
    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    bool started{false};
    std::chrono::system_clock::time_point last_esl_success{};

    void update_health(ComponentState state, std::string detail, std::string remediation) {
        if (state == ComponentState::healthy) last_esl_success = std::chrono::system_clock::now();
        health.update("esl", {
            state,
            std::move(detail),
            std::move(remediation),
            last_esl_success,
        });
    }

    void update_heartbeat() {
        health.update("heartbeat", {
            ComponentState::healthy,
            "HEARTBEAT events are arriving",
            "",
            std::chrono::system_clock::now(),
        });
    }

    void dispatch(esl_event_t* event) {
        RawEslEvent raw;
        raw.event_name = header_value(event, "Event-Name");
        if (raw.event_name.empty()) {
            raw.event_name = esl_event_name(event->event_id);
        }
        for (const char* name : {"Event-Date-Timestamp", "Event-Date-Local", "Unique-ID",
                                 "Channel-Call-UUID", "variable_uuid", "Channel-State",
                                 "Channel-Call-State", "Answer-State", "Hangup-Cause",
                                 "Application", "Event-Subclass", "Menu", "Choice"}) {
            const auto value = header_value(event, name);
            if (!value.empty()) raw.headers.emplace(name, value);
        }
        if (raw.event_name == "HEARTBEAT") update_heartbeat();
        if (handler) handler(std::move(raw));
    }

    bool connect_once() {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (!state_machine.transition(EslConnectionState::connecting) ||
                !state_machine.transition(EslConnectionState::authenticating)) {
                state_machine.failed(false);
                if (state_handler) state_handler(state_machine.state());
                update_health(
                    ComponentState::degraded,
                    "ESL observer state transition failed",
                    "restart the tutorial service");
                return false;
            }
        }

        const auto result = esl_connect_timeout(
            &handle, config.host.c_str(), config.port, "ClueCon", config.password.c_str(), 5000);
        if (result != ESL_SUCCESS) {
            const bool auth_failure = authentication_failure(handle);
            std::lock_guard<std::mutex> lock(state_mutex);
            state_machine.failed(auth_failure);
            if (state_handler) state_handler(state_machine.state());
            update_health(
                ComponentState::degraded,
                auth_failure ? "ESL authentication failed" : "ESL connection unavailable",
                auth_failure
                    ? "check the ESL password environment variable"
                    : "check the ESL host, port, and FreeSWITCH availability");
            return false;
        }

        const auto events = esl_events(
            &handle,
            ESL_EVENT_TYPE_PLAIN,
            "HEARTBEAT CUSTOM CHANNEL_CREATE CHANNEL_ANSWER CHANNEL_BRIDGE "
            "CHANNEL_HANGUP CHANNEL_HANGUP_COMPLETE CHANNEL_DESTROY");
        if (events != ESL_SUCCESS) {
            esl_disconnect(&handle);
            std::lock_guard<std::mutex> lock(state_mutex);
            state_machine.failed(false);
            if (state_handler) state_handler(state_machine.state());
            update_health(
                ComponentState::degraded,
                "ESL event subscription failed",
                "check the event socket permissions and retry");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex);
            state_machine.connected();
            if (state_handler) state_handler(state_machine.state());
            update_health(ComponentState::healthy, "ESL event connection subscribed", "");
        }
        return true;
    }

    void disconnect() {
        if (handle.connected) esl_disconnect(&handle);
        health.update("esl", {
            ComponentState::degraded,
            "ESL event connection disconnected",
            "wait for bounded reconnect or inspect FreeSWITCH availability",
            last_esl_success,
        });
    }

    void run() {
        if (!config.enabled) {
            update_health(
                ComponentState::unavailable,
                "ESL observer is disabled",
                "enable ESL only in the isolated tutorial lab");
            return;
        }

        while (!stopping) {
            if (connect_once()) {
                while (!stopping) {
                    esl_event_t* event = nullptr;
                    const auto result = esl_recv_event_timed(&handle, 1000, 1, &event);
                    if (result == ESL_SUCCESS) {
                        if (event != nullptr) {
                            dispatch(event);
                            esl_event_destroy(&event);
                        }
                        continue;
                    }
                    if (result == ESL_BREAK) continue;
                    std::lock_guard<std::mutex> lock(state_mutex);
                    state_machine.failed(false);
                    if (state_handler) state_handler(state_machine.state());
                    break;
                }
                disconnect();
            }
            if (stopping) break;

            std::chrono::milliseconds delay;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                delay = state_machine.next_retry_delay();
            }
            std::unique_lock<std::mutex> lock(wait_mutex);
            wait_cv.wait_for(lock, delay, [this] { return stopping.load(); });
        }
    }
};

EslEventClient::EslEventClient(
    EslConfig config,
    HealthRegistry& health,
    EventHandler handler,
    StateHandler state_handler)
    : impl_(
          std::make_unique<Impl>(
              std::move(config), health, std::move(handler), std::move(state_handler))) {}

EslEventClient::~EslEventClient() {
    stop();
}

void EslEventClient::start() {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    if (impl_->started) return;
    impl_->started = true;
    impl_->worker = std::thread([this] { impl_->run(); });
}

void EslEventClient::stop() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        if (!impl_->started) return;
        impl_->stopping = true;
    }
    impl_->wait_cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
}

EslConnectionState EslEventClient::state() const {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    return impl_->state_machine.state();
}

struct EslCommandClient::Impl {
    Impl(EslConfig config_value, HealthRegistry& health_value)
        : config(std::move(config_value)), health(health_value) {}

    EslConfig config;
    HealthRegistry& health;
    esl_handle_t handle{};
    std::mutex mutex;
    std::chrono::system_clock::time_point last_success{};

    bool connect() {
        const auto result = esl_connect_timeout(
            &handle, config.host.c_str(), config.port, "ClueCon", config.password.c_str(), 5000);
        if (result == ESL_SUCCESS) {
            last_success = std::chrono::system_clock::now();
            health.update("freeswitch", {
                ComponentState::healthy,
                "ESL command connection reached FreeSWITCH",
                "",
                last_success,
            });
            return true;
        }
        health.update("freeswitch", {
            ComponentState::unavailable,
            "FreeSWITCH command connection is unavailable",
            "check the FreeSWITCH host, port, and ESL credentials",
            last_success,
        });
        return false;
    }

    void disconnect() {
        if (handle.connected) esl_disconnect(&handle);
    }
};

EslCommandClient::EslCommandClient(EslConfig config, HealthRegistry& health)
    : impl_(std::make_unique<Impl>(std::move(config), health)) {}

EslCommandClient::~EslCommandClient() {
    disconnect();
}

std::optional<std::string> EslCommandClient::execute(const std::string& name) {
    const auto command = parse_command_name(name);
    if (!command) return std::nullopt;
    const auto wire_command = allowlisted_command(*command);
    if (!wire_command) return std::nullopt;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->config.enabled || (!impl_->handle.connected && !impl_->connect())) {
        return std::nullopt;
    }
    if (esl_send_recv_timed(&impl_->handle, wire_command->c_str(), 5000) != ESL_SUCCESS) {
        impl_->disconnect();
        return std::nullopt;
    }
    return std::string{impl_->handle.last_sr_reply};
}

void EslCommandClient::disconnect() {
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->disconnect();
}

}  // namespace fstutorial
