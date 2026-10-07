#include "http_server.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <crow.h>

#include "logging.h"

namespace fstutorial {
namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool safe_segment(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.find('\0') != std::string::npos) return false;
    const std::filesystem::path path{value};
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

bool safe_locale(const std::string& value) {
    static const std::regex pattern{R"(^[a-z]{2}(?:-[A-Z]{2})?$)"};
    return std::regex_match(value, pattern);
}

std::string content_type(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".js" || extension == ".mjs") return "text/javascript; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".json") return "application/json; charset=utf-8";
    if (extension == ".md") return "text/markdown; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    return "application/octet-stream";
}

crow::response file_response(const std::filesystem::path& path) {
    const std::string body = read_file(path);
    if (body.empty() && (!std::filesystem::exists(path) || std::filesystem::file_size(path) != 0)) {
        return crow::response(404, "not found");
    }
    crow::response response{body};
    response.set_header("Content-Type", content_type(path));
    response.set_header("X-Content-Type-Options", "nosniff");
    return response;
}

crow::response cached_file_response(const std::filesystem::path& path,
                                    const std::string& cache_control) {
    auto response = file_response(path);
    response.set_header("Cache-Control", cache_control);
    return response;
}

std::string event_sse(const NormalizedEvent& event) {
    crow::json::wvalue body;
    body["timestamp"] = event.timestamp;
    body["type"] = event.type;
    body["correlation_id"] = event.correlation_id;
    body["summary"] = event.summary;
    return "data: " + body.dump() + "\n\n";
}

std::int64_t epoch_seconds(std::chrono::system_clock::time_point value) {
    if (value == std::chrono::system_clock::time_point{}) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

}  // namespace

HttpServer::HttpServer(
    Config config,
    HealthRegistry& health,
    CheckRegistry& checks,
    EventHub& events,
    MetricsRegistry& metrics)
    : config_(std::move(config)),
      health_(health),
      checks_(checks),
      events_(events),
      metrics_(metrics) {}

void HttpServer::run() {
    auto app = std::make_unique<crow::SimpleApp>();
    app_ = app.get();
    app->loglevel(crow::LogLevel::Warning);

    CROW_ROUTE((*app), "/")([this]() {
        return cached_file_response(config_.web_root / "index.html", "no-cache");
    });

    CROW_ROUTE((*app), "/assets/<path>")([this](const std::string& relative) {
        if (!safe_segment(relative)) return crow::response(400, "invalid path");
        return cached_file_response(
            config_.web_root / relative, "public, max-age=3600, must-revalidate");
    });

    CROW_ROUTE((*app), "/content/<path>")([this](const std::string& relative) {
        if (!safe_segment(relative)) return crow::response(400, "invalid path");
        return cached_file_response(config_.content_root / relative, "no-cache");
    });

    CROW_ROUTE((*app), "/api/v1/lessons/<string>/<string>")
    ([this](const std::string& locale, const std::string& lesson_id) {
        if (!CheckRegistry::valid_identifier(lesson_id) || !safe_locale(locale)) {
            return crow::response(400, "invalid lesson request");
        }
        auto path = config_.content_root / locale / "days" / (lesson_id + ".md");
        bool fallback = false;
        if (!std::filesystem::is_regular_file(path)) {
            path = config_.content_root / config_.default_locale / "days" / (lesson_id + ".md");
            fallback = locale != config_.default_locale;
        }
        auto response = cached_file_response(path, "no-cache");
        response.set_header("Content-Language", fallback ? config_.default_locale : locale);
        if (fallback) response.set_header("X-Tutorial-Locale-Fallback", config_.default_locale);
        return response;
    });

    CROW_ROUTE((*app), "/api/v1/public-config")([this]() {
        crow::json::wvalue body;
        body["default_locale"] = config_.default_locale;
        body["sip"]["wss_url"] = config_.sip.wss_url;
        body["sip"]["domain"] = config_.sip.domain;
        std::vector<crow::json::wvalue> extensions;
        for (const auto& extension : config_.sip.tutorial_extensions) {
            extensions.emplace_back(extension);
        }
        body["sip"]["tutorial_extensions"] = std::move(extensions);
        crow::response response{body};
        response.set_header("Cache-Control", "no-store");
        return response;
    });

    CROW_ROUTE((*app), "/api/v1/health")([this]() {
        crow::json::wvalue body;
        body["status"] = component_state_name(health_.overall_state());
        for (const auto& [name, health] : health_.snapshot()) {
            body["components"][name]["status"] = component_state_name(health.state);
            body["components"][name]["detail"] = health.detail;
            body["components"][name]["remediation"] = health.remediation;
            body["components"][name]["last_success_epoch"] = epoch_seconds(health.last_success);
        }
        crow::response response{body};
        response.set_header("Cache-Control", "no-store");
        return response;
    });

    CROW_ROUTE((*app), "/api/v1/events")([this]() {
        const auto subscription = events_.subscribe();
        crow::response response;
        response.set_header("Content-Type", "text/event-stream; charset=utf-8");
        response.set_header("Cache-Control", "no-cache");
        response.set_header("Connection", "keep-alive");
        response.set_header("X-Accel-Buffering", "no");
        response.body = "retry: 3000\n\n";
        for (const auto& event : subscription.initial) response.body += event_sse(event);

        for (int interval = 0; interval < 4; ++interval) {
            for (const auto& event : events_.wait_for_events(
                     subscription.id, std::chrono::milliseconds{250})) {
                response.body += event_sse(event);
            }
            response.body += ": keepalive\n\n";
        }
        const auto dropped = events_.dropped_count(subscription.id);
        if (dropped > 0) {
            response.body += "event: dropped\n";
            response.body += "data: {\"drop_count\":" + std::to_string(dropped) + "}\n\n";
        }
        events_.unsubscribe(subscription.id);
        return response;
    });

    CROW_ROUTE((*app), "/metrics")([this]() {
        crow::response response{metrics_.prometheus()};
        response.set_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
        response.set_header("Cache-Control", "no-store");
        return response;
    });

    CROW_ROUTE((*app), "/api/v1/checks/<string>/<string>").methods(crow::HTTPMethod::POST)
    ([this](const crow::request& request, const std::string& lesson_id, const std::string& check_id) {
        if (!request.body.empty()) {
            return crow::response(400, R"({"error":"check does not accept a request body"})");
        }
        const auto result = checks_.run(lesson_id, check_id);
        if (!result) {
            return crow::response(404, R"({"error":"unknown declared check"})");
        }
        crow::json::wvalue body;
        body["status"] = check_state_name(result->state);
        body["evidence"] = result->evidence;
        body["remediation"] = result->remediation;
        return crow::response{body};
    });

    log_message(LogLevel::info, "http", "starting tutorial service on " +
                config_.http.host + ':' + std::to_string(config_.http.port));
    if (config_.http.tls) {
        app->ssl_file(config_.http.certificate_file.string(),
                      config_.http.private_key_file.string());
    }
    app->bindaddr(config_.http.host)
        .port(config_.http.port)
        .multithreaded()
        .run();
    log_message(LogLevel::info, "http", "tutorial service stopped");
    app_ = nullptr;
}

void HttpServer::stop() {
    if (app_ != nullptr) {
        static_cast<crow::SimpleApp*>(app_)->stop();
    }
}

}  // namespace fstutorial
