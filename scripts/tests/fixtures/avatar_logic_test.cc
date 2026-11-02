/* Host tests for the avatar logic that needs no LVGL. main selects a case by name. */
#include "avatar_clock.h"

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
