#include "config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>

#include <yaml-cpp/yaml.h>

namespace fstutorial {
namespace {

std::string default_environment(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return value == nullptr ? std::string{} : std::string{value};
}

std::uint16_t read_port(const YAML::Node& node, const char* label, std::uint16_t fallback) {
    if (!node) {
        return fallback;
    }
    int value = 0;
    try {
        value = node.as<int>();
    } catch (const YAML::Exception&) {
        throw ConfigError(std::string(label) + " must be an integer from 1 to 65535");
    }
    if (value < 1 || value > std::numeric_limits<std::uint16_t>::max()) {
        throw ConfigError(std::string(label) + " must be from 1 to 65535");
    }
    return static_cast<std::uint16_t>(value);
}

std::size_t read_limit(const YAML::Node& node, const char* label,
                       std::size_t fallback, std::size_t maximum) {
    if (!node) {
        return fallback;
    }
    long long value = 0;
    try {
        value = node.as<long long>();
    } catch (const YAML::Exception&) {
        throw ConfigError(std::string(label) + " must be a positive integer");
    }
    if (value < 1 || static_cast<unsigned long long>(value) > maximum) {
        throw ConfigError(std::string(label) + " is outside its safe bound");
    }
    return static_cast<std::size_t>(value);
}

std::filesystem::path resolve_from(const std::filesystem::path& config_path,
                                   const std::string& value) {
    std::filesystem::path result{value};
    if (result.is_relative()) {
        result = config_path.parent_path() / result;
    }
    return result.lexically_normal();
}

}  // namespace

bool is_loopback_host(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost";
}

Config load_config(const std::filesystem::path& path, EnvironmentLookup environment) {
    if (!environment) {
        environment = default_environment;
    }
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& exc) {
        throw ConfigError(std::string("cannot load configuration: ") + exc.what());
    }

    Config config;
    if (const auto http = root["http"]) {
        if (http["host"]) config.http.host = http["host"].as<std::string>();
        config.http.port = read_port(http["port"], "http.port", config.http.port);
        if (http["allow_remote"]) config.http.allow_remote = http["allow_remote"].as<bool>();
        if (http["tls"]) config.http.tls = http["tls"].as<bool>();
        if (http["certificate_file"]) {
            config.http.certificate_file = resolve_from(
                path, http["certificate_file"].as<std::string>());
        }
        if (http["private_key_file"]) {
            config.http.private_key_file = resolve_from(
                path, http["private_key_file"].as<std::string>());
        }
    }
    if (config.http.host.empty()) {
        throw ConfigError("http.host must not be empty");
    }
    if (!is_loopback_host(config.http.host) &&
        (!config.http.allow_remote || !config.http.tls)) {
        throw ConfigError("non-loopback http.host requires allow_remote and TLS");
    }
    if (config.http.tls &&
        (!std::filesystem::is_regular_file(config.http.certificate_file) ||
         !std::filesystem::is_regular_file(config.http.private_key_file))) {
        throw ConfigError("TLS requires readable certificate_file and private_key_file");
    }

    if (root["content_root"]) {
        config.content_root = resolve_from(path, root["content_root"].as<std::string>());
    }
    if (root["web_root"]) {
        config.web_root = resolve_from(path, root["web_root"].as<std::string>());
    }
    if (root["sound_root"]) {
        config.sound_root = resolve_from(path, root["sound_root"].as<std::string>());
    }
    if (root["default_locale"]) {
        config.default_locale = root["default_locale"].as<std::string>();
    }
    if (config.default_locale != "zh-CN") {
        throw ConfigError("default_locale must be zh-CN for the initial tutorial");
    }

    if (const auto sip = root["sip"]) {
        if (sip["wss_url"]) config.sip.wss_url = sip["wss_url"].as<std::string>();
        if (sip["domain"]) config.sip.domain = sip["domain"].as<std::string>();
        if (sip["tutorial_extensions"]) {
            config.sip.tutorial_extensions = sip["tutorial_extensions"].as<std::vector<std::string>>();
        }
    }
    if (config.sip.wss_url.rfind("wss://", 0) != 0) {
        throw ConfigError("sip.wss_url must use wss://");
    }
    if (config.sip.domain.empty() || config.sip.tutorial_extensions.empty()) {
        throw ConfigError("sip.domain and sip.tutorial_extensions must not be empty");
    }

    if (const auto esl = root["esl"]) {
        if (esl["enabled"]) config.esl.enabled = esl["enabled"].as<bool>();
        if (esl["host"]) config.esl.host = esl["host"].as<std::string>();
        config.esl.port = read_port(esl["port"], "esl.port", config.esl.port);
        if (esl["password_env"]) config.esl.password_env = esl["password_env"].as<std::string>();
    }
    if (config.esl.enabled) {
        if (config.esl.password_env.empty()) {
            throw ConfigError("esl.password_env is required when ESL is enabled");
        }
        config.esl.password = environment(config.esl.password_env);
        if (config.esl.password.empty()) {
            throw ConfigError("configured ESL password environment variable is missing");
        }
    }

    if (const auto limits = root["limits"]) {
        config.limits.recent_events = read_limit(
            limits["recent_events"], "limits.recent_events", config.limits.recent_events, 100000);
        config.limits.client_queue = read_limit(
            limits["client_queue"], "limits.client_queue", config.limits.client_queue, 10000);
        config.limits.call_correlations = read_limit(
            limits["call_correlations"], "limits.call_correlations", config.limits.call_correlations, 1000000);
    }
    return config;
}

}  // namespace fstutorial
