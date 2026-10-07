#include "redaction.h"

#include <cstdint>
#include <iomanip>
#include <regex>
#include <sstream>

namespace fstutorial {

std::string safe_correlation_id(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << "corr-" << std::hex << std::setw(12) << std::setfill('0')
           << (hash & 0xffffffffffffULL);
    return stream.str();
}

std::string redact_sensitive(std::string value) {
    static const std::regex private_key(
        "-----BEGIN [^-]*PRIVATE KEY-----[\\s\\S]*?-----END [^-]*PRIVATE KEY-----",
        std::regex::icase);
    static const std::regex authorization_header(
        R"(((?:Proxy-)?Authorization)\s*:\s*[^\r\n]+)",
        std::regex::icase);
    static const std::regex secret_field(
        R"((password|passwd|nonce)\s*[:=]\s*[^\s,;]+)",
        std::regex::icase);
    static const std::regex uuid(
        R"(\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b)");
    static const std::regex ipv4(R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)");
    static const std::regex phone_number(R"(\+?[0-9]{7,15})");
    static const std::regex sdp_line(
        R"((^|\n)(v=0|o=|s=|c=IN\s|m=audio\s|a=(?:candidate|fingerprint|ice-ufrag|ice-pwd):)[^\n]*)",
        std::regex::icase);

    value = std::regex_replace(value, private_key, "[REDACTED PRIVATE KEY]");
    value = std::regex_replace(value, authorization_header, "$1: [REDACTED]");
    value = std::regex_replace(value, secret_field, "$1: [REDACTED]");

    std::string result;
    std::sregex_iterator cursor(value.begin(), value.end(), uuid);
    const std::sregex_iterator end;
    std::size_t previous = 0;
    for (; cursor != end; ++cursor) {
        const auto& match = *cursor;
        result.append(value, previous, static_cast<std::size_t>(match.position()) - previous);
        result += safe_correlation_id(match.str());
        previous = static_cast<std::size_t>(match.position() + match.length());
    }
    result.append(value, previous, std::string::npos);
    result = std::regex_replace(result, sdp_line, "$1[REDACTED SDP]");
    result = std::regex_replace(result, ipv4, "[IP]");
    result = std::regex_replace(result, phone_number, "[NUMBER]");
    return result;
}

}  // namespace fstutorial
