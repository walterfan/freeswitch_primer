#include <cassert>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "check_registry.h"
#include "config.h"
#include "esl_observer.h"
#include "health.h"
#include "logging.h"
#include "metrics.h"
#include "redaction.h"

namespace {

std::filesystem::path write_config(const std::string& name, const std::string& body) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream stream(path);
    stream << body;
    return path;
}

void test_config() {
    const auto valid = write_config("fstutorial-valid.yaml", R"(
http: {host: 127.0.0.1, port: 7009, allow_remote: false, tls: false}
content_root: ../content
web_root: ../web
default_locale: zh-CN
sip:
  wss_url: wss://127.0.0.1:7443
  domain: 127.0.0.1
  tutorial_extensions: ["1000", "5000"]
esl: {enabled: true, host: 127.0.0.1, port: 8021, password_env: TEST_ESL_PASSWORD}
limits: {recent_events: 10, client_queue: 5, call_correlations: 20}
)");
    const auto config = fstutorial::load_config(valid, [](const std::string& name) {
        return name == "TEST_ESL_PASSWORD" ? "secret-for-test" : "";
    });
    assert(config.http.port == 7009);
    assert(config.esl.password == "secret-for-test");
    assert(config.limits.client_queue == 5);

    bool missing_secret = false;
    try {
        (void)fstutorial::load_config(valid, [](const std::string&) { return ""; });
    } catch (const fstutorial::ConfigError& exc) {
        missing_secret = std::string{exc.what()}.find("secret-for-test") == std::string::npos;
    }
    assert(missing_secret);

    const auto invalid_port = write_config("fstutorial-invalid-port.yaml", R"(
http: {host: 127.0.0.1, port: 70000}
)");
    bool rejected_port = false;
    try {
        (void)fstutorial::load_config(invalid_port);
    } catch (const fstutorial::ConfigError&) {
        rejected_port = true;
    }
    assert(rejected_port);

    const auto unsafe_bind = write_config("fstutorial-unsafe-bind.yaml", R"(
http: {host: 0.0.0.0, port: 7009, allow_remote: false, tls: false}
)");
    bool rejected_bind = false;
    try {
        (void)fstutorial::load_config(unsafe_bind);
    } catch (const fstutorial::ConfigError&) {
        rejected_bind = true;
    }
    assert(rejected_bind);

    const auto missing_tls_files = write_config("fstutorial-missing-tls-files.yaml", R"(
http: {host: localhost, port: 9443, tls: true, certificate_file: absent.pem, private_key_file: absent-key.pem}
)");
    bool rejected_tls = false;
    try {
        (void)fstutorial::load_config(missing_tls_files);
    } catch (const fstutorial::ConfigError& exc) {
        rejected_tls = std::string{exc.what()}.find("PRIVATE KEY") == std::string::npos;
    }
    assert(rejected_tls);
}

void test_checks() {
    fstutorial::CheckRegistry registry;
    assert(registry.add("day-01", "service-health", [] {
        return fstutorial::CheckResult{fstutorial::CheckState::pass, "ok", ""};
    }));
    assert(!registry.add("day-01", "service-health", [] { return fstutorial::CheckResult{}; }));
    assert(!registry.add("day-01", "../../shell", [] { return fstutorial::CheckResult{}; }));
    assert(registry.run("day-01", "service-health")->state == fstutorial::CheckState::pass);
    assert(!registry.run("day-01", "unknown"));
}

void test_health() {
    fstutorial::HealthRegistry health;
    assert(health.overall_state() == fstutorial::ComponentState::unavailable);
    const auto now = std::chrono::system_clock::now();
    for (const char* component : {"freeswitch", "esl", "heartbeat", "sip_profile", "metrics"}) {
        health.update(component, {fstutorial::ComponentState::healthy, "ok", "", now});
    }
    assert(health.overall_state() == fstutorial::ComponentState::healthy);
    health.update("heartbeat", {fstutorial::ComponentState::stale, "old", "wait", now});
    assert(health.overall_state() == fstutorial::ComponentState::stale);
    health.update("esl", {fstutorial::ComponentState::degraded, "auth", "check secret", {}});
    assert(health.overall_state() == fstutorial::ComponentState::stale);
    assert(std::string{fstutorial::component_state_name(fstutorial::ComponentState::healthy)} == "healthy");
    assert(std::string{fstutorial::component_state_name(fstutorial::ComponentState::degraded)} == "degraded");
    assert(std::string{fstutorial::component_state_name(fstutorial::ComponentState::stale)} == "stale");
    assert(std::string{fstutorial::component_state_name(fstutorial::ComponentState::unavailable)} == "unavailable");
}

void test_redaction() {
    const std::string uuid = "123e4567-e89b-12d3-a456-426614174000";
    const std::string private_key =
        "-----BEGIN PRIVATE KEY-----\nprivate-material\n-----END PRIVATE KEY-----";
    const std::string input =
        "Authorization: Digest username=1000,response=abc\n"
        "Proxy-Authorization: Basic proxy-secret\n"
        "password=hunter2 nonce=deadbeef private=" + private_key + " "
        "ip=192.0.2.10 phone=15551234567 uuid=" + uuid +
        "\nv=0\nm=audio 20000 RTP/SAVPF 111";
    const std::string output = fstutorial::redact_sensitive(input);
    assert(output.find("response=abc") == std::string::npos);
    assert(output.find("proxy-secret") == std::string::npos);
    assert(output.find("hunter2") == std::string::npos);
    assert(output.find("deadbeef") == std::string::npos);
    assert(output.find("private-material") == std::string::npos);
    assert(output.find("192.0.2.10") == std::string::npos);
    assert(output.find("15551234567") == std::string::npos);
    assert(output.find(uuid) == std::string::npos);
    assert(output.find("m=audio") == std::string::npos);
    assert(output.find("corr-") != std::string::npos);

    const std::string log = fstutorial::format_log_message(
        fstutorial::LogLevel::warning, "sip",
        "Authorization: Digest response=abc\nfrom 192.0.2.10 to +15551234567", uuid);
    assert(log.find("response=abc") == std::string::npos);
    assert(log.find("192.0.2.10") == std::string::npos);
    assert(log.find("15551234567") == std::string::npos);
    assert(log.find(uuid) == std::string::npos);
    assert(log.find("corr-") != std::string::npos);
}

void test_esl_observer() {
    fstutorial::EslConnectionStateMachine state;
    assert(state.state() == fstutorial::EslConnectionState::disconnected);
    assert(state.transition(fstutorial::EslConnectionState::connecting));
    assert(state.transition(fstutorial::EslConnectionState::authenticating));
    assert(state.transition(fstutorial::EslConnectionState::subscribed));
    assert(!state.transition(fstutorial::EslConnectionState::connecting));
    state.failed(true);
    assert(state.state() == fstutorial::EslConnectionState::degraded);
    assert(state.failure_detail() == "ESL authentication failed");
    assert(state.transition(fstutorial::EslConnectionState::connecting));
    assert(state.transition(fstutorial::EslConnectionState::authenticating));
    assert(state.transition(fstutorial::EslConnectionState::subscribed));
    assert(state.transition(fstutorial::EslConnectionState::disconnected));
    assert(state.transition(fstutorial::EslConnectionState::connecting));
    assert(state.transition(fstutorial::EslConnectionState::authenticating));
    assert(state.transition(fstutorial::EslConnectionState::subscribed));
    assert(state.next_retry_delay() == std::chrono::milliseconds{250});
    assert(state.next_retry_delay() == std::chrono::milliseconds{500});
    state.connected();
    assert(state.state() == fstutorial::EslConnectionState::subscribed);
    assert(state.next_retry_delay() == std::chrono::milliseconds{250});

    fstutorial::RawEslEvent raw{
        "CHANNEL_HANGUP",
        {
            {"Event-Date-Timestamp", "20260830123456"},
            {"Unique-ID", "123e4567-e89b-12d3-a456-426614174000"},
            {"Answer-State", "hangup"},
            {"Hangup-Cause", "NORMAL_CLEARING"},
            {"raw-sdp", "v=0"},
            {"Authorization", "Digest secret"},
            {"Caller-Number", "15551234567"},
            {"Private-Key", "private-material"},
            {"Remote-Address", "192.0.2.10"},
        },
    };
    const auto normalized = fstutorial::normalize_event(raw);
    assert(normalized.type == "CHANNEL_HANGUP");
    assert(normalized.correlation_id.rfind("corr-", 0) == 0);
    assert(normalized.fields.count("Answer-State") == 1);
    assert(normalized.fields.count("raw-sdp") == 0);
    assert(normalized.fields.count("Authorization") == 0);
    assert(normalized.fields.count("Caller-Number") == 0);
    assert(normalized.fields.count("Private-Key") == 0);
    assert(normalized.fields.count("Remote-Address") == 0);
    assert(normalized.correlation_id.find("123e4567") == std::string::npos);

    fstutorial::RecentEventRing ring{2};
    ring.push(normalized);
    ring.push(normalized);
    ring.push(normalized);
    assert(ring.snapshot().size() == 2);
    assert(ring.dropped_count() == 1);
    fstutorial::EventDeliveryQueue queue{1};
    queue.push(normalized);
    queue.push(normalized);
    assert(queue.drain().size() == 1);
    assert(queue.dropped_count() == 1);

    fstutorial::EventHub hub{2, 1};
    const auto subscription = hub.subscribe();
    hub.publish(raw);
    assert(hub.wait_for_events(subscription.id, std::chrono::milliseconds{1}).size() == 1);
    hub.publish(raw);
    hub.publish(raw);
    assert(hub.dropped_count() == 1);
    assert(hub.drain(subscription.id).size() == 1);
    hub.unsubscribe(subscription.id);
    assert(hub.drain(subscription.id).empty());

    std::string executed;
    std::atomic<int> active_commands{0};
    std::atomic<int> maximum_active_commands{0};
    fstutorial::SerializedCommandConnection commands{
        [&executed, &active_commands, &maximum_active_commands](const std::string& command) {
            executed = command;
            const int active = ++active_commands;
            int maximum = maximum_active_commands.load();
            while (active > maximum &&
                   !maximum_active_commands.compare_exchange_weak(maximum, active)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            --active_commands;
            return std::optional<std::string>{"ok"};
        }};
    assert(commands.execute("status") == std::optional<std::string>{"ok"});
    assert(executed == "status");
    assert(commands.execute("api shutdown") == std::nullopt);
    assert(commands.execute("sofia_profile_status") == std::optional<std::string>{"ok"});
    assert(executed == "sofia status");
    std::thread first{[&commands] { assert(commands.execute("status")); }};
    std::thread second{[&commands] { assert(commands.execute("tutorial_metrics")); }};
    first.join();
    second.join();
    assert(maximum_active_commands == 1);
}

void test_metrics() {
    fstutorial::HealthRegistry health;
    fstutorial::MetricsRegistry metrics{
        health, 8, {{"main", {"1", "2"}}, {"submenu", {"1"}}}};
    assert(!metrics.snapshot().freeswitch_up);
    assert(metrics.snapshot().stale);

    fstutorial::RawEslEvent heartbeat{
        "HEARTBEAT",
        {{"Session-Count", "2"}, {"Session-Since-Startup", "9"}}};
    metrics.set_esl_connected(true);
    metrics.record_status("UP 0 years, 0 sessions, Max-Sessions: 100");
    metrics.record_event(heartbeat);
    auto snapshot = metrics.snapshot();
    assert(snapshot.freeswitch_up);
    assert(snapshot.esl_connected);
    assert(snapshot.sessions_current == 2);
    assert(snapshot.sessions_total == 9);
    assert(snapshot.sessions_capacity_known);
    assert(snapshot.sessions_capacity == 100);
    assert(!snapshot.stale);

    fstutorial::RawEslEvent created{
        "CHANNEL_CREATE",
        {{"Unique-ID", "123e4567-e89b-12d3-a456-426614174000"}}};
    fstutorial::RawEslEvent answered{
        "CHANNEL_ANSWER",
        {{"Unique-ID", "123e4567-e89b-12d3-a456-426614174000"}}};
    fstutorial::RawEslEvent hangup{
        "CHANNEL_HANGUP",
        {
            {"Unique-ID", "123e4567-e89b-12d3-a456-426614174000"},
            {"Hangup-Cause", "NORMAL_CLEARING"},
        }};
    metrics.record_event(created);
    metrics.record_event(created);
    metrics.record_event(answered);
    metrics.record_event(hangup);
    metrics.record_event(hangup);
    metrics.record_event(fstutorial::RawEslEvent{
        "CHANNEL_DESTROY",
        {{"Unique-ID", "123e4567-e89b-12d3-a456-426614174000"}}});
    snapshot = metrics.snapshot();
    assert(snapshot.sessions_current == 2);
    assert(snapshot.calls_total.at("answered") == 1);
    assert(snapshot.calls_total.at("failed") == 0);

    metrics.record_event(fstutorial::RawEslEvent{
        "CUSTOM",
        {
            {"Event-Subclass", "tutorial::ivr_choice"},
            {"Menu", "main"},
            {"Choice", "1"},
        }});
    metrics.record_ivr_choice("untrusted-menu", "password");
    snapshot = metrics.snapshot();
    assert(snapshot.ivr_choice_total.at({"main", "1"}) == 1);
    assert(snapshot.ivr_choice_total.at({"other", "other"}) == 1);

    const auto exposition = metrics.prometheus();
    for (const char* family : {
             "freeswitch_up", "freeswitch_esl_connected", "freeswitch_sessions_current",
             "freeswitch_sessions_total", "freeswitch_calls_total",
             "freeswitch_call_duration_seconds", "freeswitch_heartbeat_age_seconds",
             "freeswitch_ivr_choice_total", "freeswitch_sessions_capacity"}) {
        assert(exposition.find(family) != std::string::npos);
    }
    assert(exposition.find("123e4567") == std::string::npos);
    metrics.set_esl_connected(false);
    assert(!metrics.snapshot().freeswitch_up);

    fstutorial::MetricsRegistry malformed{health, 8};
    malformed.set_esl_connected(true);
    malformed.record_event(fstutorial::RawEslEvent{
        "HEARTBEAT", {{"Session-Count", "not-a-counter"}}});
    const auto malformed_snapshot = malformed.snapshot();
    assert(!malformed_snapshot.sessions_current_known);
    assert(!malformed_snapshot.sessions_total_known);
    assert(malformed_snapshot.stale == false);
    assert(malformed.prometheus().find("freeswitch_sessions_current NaN") != std::string::npos);
    assert(health.snapshot().at("metrics").state == fstutorial::ComponentState::degraded);
}

}  // namespace

int main() {
    test_config();
    test_checks();
    test_health();
    test_redaction();
    test_esl_observer();
    test_metrics();
    return 0;
}
