#ifndef SERVER_WAIT_H
#define SERVER_WAIT_H

#include "server_endpoint.h"

#include <functional>
#include <string>

// What the wait for the server needs from the device, so host tests can run it.
struct ServerWaitHooks {
    std::function<bool()> still_required;
    std::function<std::string()> stored_url;
    std::function<bool(const ServerEndpoint&)> probe;
    // Asks the server for its settings. One answer brings the URL and the time.
    std::function<void()> check_settings;
    // True while the wait is failing, so power save can run.
    std::function<void(bool)> set_waiting;
    // Shows one second of the countdown, and takes that second.
    std::function<void(int)> show_failure;
    std::function<void()> show_searching;
};

// Returns once the server answers. It retries without limit and never reads the
// device state, because a touch moves Activating to Idle during the wait.
void WaitForServer(const ServerWaitHooks& hooks);

#endif  // SERVER_WAIT_H
