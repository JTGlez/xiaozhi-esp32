#include "boot_view.h"

// The built-in font has no ellipsis or heavy cross, so plain dots and U+00D7.
BootView InitialBootView() { return {"Conectando a Wi-Fi...", false, 0}; }

BootView ResolveBootView(BootStep step, int retry_seconds) {
    switch (step) {
        case BootStep::kFindingServer:
            return {"Buscando al servidor...", false, 0};
        case BootStep::kServerFailed:
            return {"\xC3\x97 No encuentro el servidor", true, retry_seconds};
        case BootStep::kReady:
            break;
    }
    return {"", false, 0};
}

std::string BootCountdownText(int retry_seconds) {
    if (retry_seconds <= 0) {
        return "";
    }
    return "Reintento en " + std::to_string(retry_seconds) + " s";
}
