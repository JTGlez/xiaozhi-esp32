#ifndef SERVER_ENDPOINT_H
#define SERVER_ENDPOINT_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct ServerEndpoint {
    std::string host;
    uint16_t port = 0;
};

// The host and port of a ws:// or wss:// URL. IPv6 literals are not supported.
std::optional<ServerEndpoint> ParseWebsocketEndpoint(std::string_view url);

#endif  // SERVER_ENDPOINT_H
