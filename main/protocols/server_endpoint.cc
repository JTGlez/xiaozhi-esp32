#include "server_endpoint.h"

std::optional<ServerEndpoint> ParseWebsocketEndpoint(std::string_view url) {
    static constexpr std::string_view kPlain = "ws://";
    static constexpr std::string_view kSecure = "wss://";
    uint16_t port = 0;
    if (url.substr(0, kPlain.size()) == kPlain) {
        url.remove_prefix(kPlain.size());
        port = 80;
    } else if (url.substr(0, kSecure.size()) == kSecure) {
        url.remove_prefix(kSecure.size());
        port = 443;
    } else {
        return std::nullopt;
    }

    size_t host_end = url.find_first_of(":/");
    std::string_view host = url.substr(0, host_end);
    if (host.empty() || host.front() == '[') {
        return std::nullopt;
    }
    if (host_end != std::string_view::npos && url[host_end] == ':') {
        std::string_view digits = url.substr(host_end + 1);
        digits = digits.substr(0, digits.find('/'));
        if (digits.empty()) {
            return std::nullopt;
        }
        uint32_t value = 0;
        for (char digit : digits) {
            if (digit < '0' || digit > '9') {
                return std::nullopt;
            }
            value = value * 10 + static_cast<uint32_t>(digit - '0');
            if (value > 65535) {
                return std::nullopt;
            }
        }
        if (value == 0) {
            return std::nullopt;
        }
        port = static_cast<uint16_t>(value);
    }
    return ServerEndpoint{std::string(host), port};
}
