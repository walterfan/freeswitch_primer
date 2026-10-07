#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fstutorial {

struct HttpConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{7009};
    bool allow_remote{false};
    bool tls{false};
    std::filesystem::path certificate_file;
    std::filesystem::path private_key_file;
};

struct SipConfig {
    std::string wss_url{"wss://127.0.0.1:7443"};
    std::string domain{"127.0.0.1"};
    std::vector<std::string> tutorial_extensions{"1000", "1001", "5000"};
};

struct EslConfig {
    bool enabled{false};
    std::string host{"127.0.0.1"};
    std::uint16_t port{8021};
    std::string password;
    std::string password_env{"FS_TUTORIAL_ESL_PASSWORD"};
};

struct LimitConfig {
    std::size_t recent_events{1000};
    std::size_t client_queue{256};
    std::size_t call_correlations{4096};
};

struct Config {
    HttpConfig http;
    SipConfig sip;
    EslConfig esl;
    LimitConfig limits;
    std::filesystem::path content_root{"content"};
    std::filesystem::path web_root{"web"};
    std::filesystem::path sound_root{"labs/day-16/sounds"};
    std::string default_locale{"zh-CN"};
};

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using EnvironmentLookup = std::function<std::string(const std::string&)>;

Config load_config(const std::filesystem::path& path,
                   EnvironmentLookup environment = {});
bool is_loopback_host(const std::string& host);

}  // namespace fstutorial
