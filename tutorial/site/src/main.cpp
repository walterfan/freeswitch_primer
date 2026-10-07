#include <csignal>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "check_registry.h"
#include "config.h"
#ifdef TUTORIAL_ENABLE_ESL
#include "esl_client.h"
#endif
#include "health.h"
#include "http_server.h"
#include "logging.h"
#include "redaction.h"

namespace {

fstutorial::HttpServer* active_server = nullptr;

void handle_signal(int) {
    if (active_server != nullptr) active_server->stop();
}

std::filesystem::path config_path(int argc, char** argv) {
    if (argc == 1) return "config/config.yaml";
    if (argc == 3 && std::string{argv[1]} == "--config") return argv[2];
    throw fstutorial::ConfigError("usage: freeswitch-tutorial-site [--config <path>]");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        auto config = fstutorial::load_config(config_path(argc, argv));
        fstutorial::HealthRegistry health;
        fstutorial::EventHub events{config.limits.recent_events, config.limits.client_queue};
        fstutorial::MetricsRegistry metrics{
            health,
            config.limits.call_correlations,
            {{"main", {"1", "2", "3"}}, {"submenu", {"1", "2"}}}};
#ifdef TUTORIAL_ENABLE_ESL
        fstutorial::EslEventClient esl_client{
            config.esl, health, [&events, &metrics](fstutorial::RawEslEvent event) {
                metrics.set_esl_connected(true);
                metrics.record_event(event);
                events.publish(std::move(event));
            }, [&metrics](fstutorial::EslConnectionState state) {
                metrics.set_esl_connected(state == fstutorial::EslConnectionState::subscribed);
            }};
        fstutorial::EslCommandClient command_client{config.esl, health};
        esl_client.start();
#endif
        fstutorial::CheckRegistry checks;
        std::function<std::optional<std::string>(const std::string&)> run_command;
#ifdef TUTORIAL_ENABLE_ESL
        run_command = [&command_client](const std::string& name) {
            return command_client.execute(name);
        };
#endif
        checks.add("day-01", "service-health", [&health]() {
            const auto state = health.overall_state();
            return fstutorial::CheckResult{
                state == fstutorial::ComponentState::healthy
                    ? fstutorial::CheckState::pass
                    : fstutorial::CheckState::unavailable,
                std::string{"overall health is "} + fstutorial::component_state_name(state),
                state == fstutorial::ComponentState::healthy
                    ? ""
                    : "open /api/v1/health and inspect the unavailable component"};
        });

        const auto command_check = [&run_command, &metrics, &health](const std::string& command_name) {
            return [&run_command, &metrics, &health, command_name]() {
                if (!run_command) {
                    return fstutorial::CheckResult{
                        fstutorial::CheckState::unavailable,
                        "ESL command client is not built",
                        "configure the tutorial site with TUTORIAL_ENABLE_ESL=ON"};
                }
                const auto reply = run_command(command_name);
                if (!reply) {
                    return fstutorial::CheckResult{
                        fstutorial::CheckState::unavailable,
                        "ESL command is unavailable",
                        "check FreeSWITCH reachability and ESL authentication"};
                }
                metrics.set_esl_connected(true);
                if (command_name == "status") metrics.record_status(*reply);
                if (command_name == "sofia_profile_status") {
                    health.update("sip_profile", {
                        reply->empty()
                            ? fstutorial::ComponentState::degraded
                            : fstutorial::ComponentState::healthy,
                        reply->empty() ? "Sofia profile status is empty" : "Sofia profile status received",
                        reply->empty() ? "check mod_sofia and the internal profile" : "",
                        std::chrono::system_clock::now(),
                    });
                }
                return fstutorial::CheckResult{
                    reply->empty() ? fstutorial::CheckState::fail : fstutorial::CheckState::pass,
                    fstutorial::redact_sensitive(*reply),
                    reply->empty() ? "inspect the command connection" : ""};
            };
        };
        checks.add("day-02", "freeswitch-status", command_check("status"));
        checks.add("day-03", "module-inventory", command_check("tutorial_metrics"));
        checks.add("day-07", "sip-registration", command_check("sofia_profile_status"));
        checks.add("day-22", "module-api", command_check("tutorial_metrics"));
        const auto sound_root = config.sound_root;
        checks.add("day-16", "ivr-sounds", [sound_root]() {
            const std::array<const char*, 3> required_files{
                "playback.wav", "phrase.wav", "ivr-main.wav"};
            std::vector<std::string> missing;
            for (const auto* filename : required_files) {
                if (!std::filesystem::is_regular_file(sound_root / filename)) {
                    missing.emplace_back(filename);
                }
            }
            if (!missing.empty()) {
                std::string names;
                for (const auto& filename : missing) {
                    if (!names.empty()) names += ", ";
                    names += filename;
                }
                return fstutorial::CheckResult{
                    fstutorial::CheckState::unavailable,
                    "missing tutorial sound assets: " + names,
                    "install the isolated tutorial sound package under the configured sound root"};
            }
            return fstutorial::CheckResult{
                fstutorial::CheckState::pass,
                "playback, phrase, and IVR sound assets are present",
                ""};
        });

        fstutorial::HttpServer server{std::move(config), health, checks, events, metrics};
        active_server = &server;
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        server.run();
#ifdef TUTORIAL_ENABLE_ESL
        esl_client.stop();
#endif
        active_server = nullptr;
        return 0;
    } catch (const fstutorial::ConfigError& exc) {
        fstutorial::log_message(
            fstutorial::LogLevel::error, "configuration", exc.what());
        return 2;
    } catch (const std::exception& exc) {
        fstutorial::log_message(fstutorial::LogLevel::error, "service", exc.what());
        return 1;
    }
}
