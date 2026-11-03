/* Host tests for the avatar logic that needs no LVGL. main selects a case by name. */
#include "avatar_clock.h"
#include "boot_view.h"
#include "server_endpoint.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "CHECK FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

namespace {

void clock_hidden_at_1970() {
    char text[8];
    CHECK(!FormatAvatarClock(0, text, sizeof(text)));
}

void clock_hidden_at_2024() {
    char text[8];
    CHECK(!FormatAvatarClock(1735689599, text, sizeof(text)));  // 2024-12-31 23:59:59 UTC
}

void clock_formats_time() {
    char text[8];
    CHECK(FormatAvatarClock(1793629920, text, sizeof(text)));  // 2026-11-02 14:32:00 UTC
    CHECK(std::strcmp(text, "14:32") == 0);
}

void clock_small_buffer_is_safe() {
    char text[8] = "XXXXXXX";
    CHECK(!FormatAvatarClock(1793629920, text, 3));
    CHECK(text[3] == 'X');
}

void bubble_width_keeps_short_text() { CHECK(AvatarBubbleLabelWidth(100, 176) == 101); }
void bubble_width_caps_long_text() { CHECK(AvatarBubbleLabelWidth(500, 176) == 176); }
void bubble_width_never_zero() { CHECK(AvatarBubbleLabelWidth(0, 176) == 1); }

void parse_ws_with_port() {
    auto endpoint = ParseWebsocketEndpoint("ws://192.0.2.1:8000/xiaozhi/v1/");
    CHECK(endpoint.has_value());
    CHECK(endpoint->host == "192.0.2.1");
    CHECK(endpoint->port == 8000);
}

void parse_defaults() {
    auto plain = ParseWebsocketEndpoint("ws://example.test/");
    CHECK(plain.has_value() && plain->host == "example.test" && plain->port == 80);
    auto secure = ParseWebsocketEndpoint("wss://example.test");
    CHECK(secure.has_value() && secure->host == "example.test" && secure->port == 443);
}

void parse_limits() {
    auto low = ParseWebsocketEndpoint("ws://x:1");
    CHECK(low.has_value() && low->port == 1);
    auto high = ParseWebsocketEndpoint("wss://x:65535/path");
    CHECK(high.has_value() && high->port == 65535);
}

void parse_rejects() {
    for (const char* url : {"", "http://x", "ws://", "ws://:80", "ws://x:0", "ws://x:65536",
                            "ws://x:80a", "ws://x:", "ws://x:99999999999999999999",
                            "WS://x", "ws://[::1]:80"}) {
        if (ParseWebsocketEndpoint(url).has_value()) {
            std::fprintf(stderr, "accepted: %s\n", url);
            std::exit(1);
        }
    }
}

void boot_views() {
    auto initial = InitialBootView();
    CHECK(std::strcmp(initial.bubble, "Conectando a Wi-Fi...") == 0);
    CHECK(!initial.is_error && initial.retry_seconds == 0);
    auto finding = ResolveBootView(BootStep::kFindingServer, 0);
    CHECK(std::strcmp(finding.bubble, "Buscando al servidor...") == 0);
    CHECK(!finding.is_error && finding.retry_seconds == 0);
    auto failed = ResolveBootView(BootStep::kServerFailed, 8);
    CHECK(std::strcmp(failed.bubble, "\xC3\x97 No encuentro el servidor") == 0);
    CHECK(failed.is_error && failed.retry_seconds == 8);
    auto ready = ResolveBootView(BootStep::kReady, 0);
    CHECK(std::strcmp(ready.bubble, "") == 0);
    CHECK(!ready.is_error && ready.retry_seconds == 0);
}

void boot_countdown() {
    CHECK(BootCountdownText(8) == "Reintento en 8 s");
    CHECK(BootCountdownText(1) == "Reintento en 1 s");
    CHECK(BootCountdownText(0).empty());
}

void boot_copy_has_only_available_glyphs() {
    std::string copy = InitialBootView().bubble;
    for (BootStep step : {BootStep::kFindingServer, BootStep::kServerFailed, BootStep::kReady}) {
        copy += ResolveBootView(step, 8).bubble;
    }
    copy += BootCountdownText(8);
    CHECK(copy.find("\xE2\x80\xA6") == std::string::npos);  // Horizontal ellipsis
    CHECK(copy.find("\xE2\x9C\x95") == std::string::npos);  // Multiplication X
}

const std::map<std::string, std::function<void()>>& cases() {
    static const std::map<std::string, std::function<void()>> all = {
#define CASE(name) {#name, name}
        CASE(clock_hidden_at_1970),
        CASE(clock_hidden_at_2024),
        CASE(clock_formats_time),
        CASE(clock_small_buffer_is_safe),
        CASE(bubble_width_keeps_short_text),
        CASE(bubble_width_caps_long_text),
        CASE(bubble_width_never_zero),
        CASE(parse_ws_with_port),
        CASE(parse_defaults),
        CASE(parse_limits),
        CASE(parse_rejects),
        CASE(boot_views),
        CASE(boot_countdown),
        CASE(boot_copy_has_only_available_glyphs),
#undef CASE
    };
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    setenv("TZ", "UTC0", 1);
    tzset();
    if (argc < 2 || cases().find(argv[1]) == cases().end()) {
        std::fprintf(stderr, "unknown case\n");
        return 2;
    }
    cases().at(argv[1])();
    std::printf("PASS %s\n", argv[1]);
    return 0;
}
