#include "server_wait.h"

namespace {
constexpr int kRetrySeconds = 8;
}

void WaitForServer(const ServerWaitHooks& hooks) {
    // After a failed attempt nothing has set the time or the config: the answer
    // that would have brought them never came.
    bool settings_missed = false;
    while (hooks.still_required()) {
        auto endpoint = ParseWebsocketEndpoint(hooks.stored_url());
        if (endpoint && hooks.probe(*endpoint)) {
            break;
        }
        hooks.set_waiting(true);
        settings_missed = true;
        for (int seconds = kRetrySeconds; seconds > 0; --seconds) {
            hooks.show_failure(seconds);
        }
        hooks.show_searching();
        // Without a URL there is nothing to probe: only the server's answer brings one.
        if (!ParseWebsocketEndpoint(hooks.stored_url())) {
            hooks.check_settings();
            settings_missed = false;
        }
    }
    hooks.set_waiting(false);
    if (settings_missed) {
        hooks.check_settings();
    }
}
