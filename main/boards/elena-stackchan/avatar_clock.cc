#include "avatar_clock.h"

bool FormatAvatarClock(time_t now, char* out, size_t size) {
    struct tm local;
    if (localtime_r(&now, &local) == nullptr || local.tm_year < 2025 - 1900) {
        return false;
    }
    return strftime(out, size, "%H:%M", &local) != 0;
}
