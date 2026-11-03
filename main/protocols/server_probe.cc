#include "server_probe.h"

#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <cerrno>
#include <string>

bool ProbeServer(const ServerEndpoint& endpoint, int timeout_ms) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* address = nullptr;
    const std::string port = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &address) != 0 ||
        address == nullptr) {
        return false;
    }

    bool reachable = false;
    int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0) {
            if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
                reachable = true;
            } else if (errno == EINPROGRESS) {
                fd_set writable;
                FD_ZERO(&writable);
                FD_SET(fd, &writable);
                timeval timeout = {};
                timeout.tv_sec = timeout_ms / 1000;
                timeout.tv_usec = (timeout_ms % 1000) * 1000;
                if (select(fd + 1, nullptr, &writable, nullptr, &timeout) > 0) {
                    int error = 0;
                    socklen_t length = sizeof(error);
                    reachable = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
                                error == 0;
                }
            }
        }
        close(fd);
    }
    freeaddrinfo(address);
    return reachable;
}
