#ifndef SERVER_PROBE_H
#define SERVER_PROBE_H

#include "server_endpoint.h"

// Whether a TCP connection to the endpoint opens within the timeout. Sends nothing.
bool ProbeServer(const ServerEndpoint& endpoint, int timeout_ms);

#endif  // SERVER_PROBE_H
