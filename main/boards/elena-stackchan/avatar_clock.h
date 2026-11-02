#ifndef AVATAR_CLOCK_H
#define AVATAR_CLOCK_H

#include <algorithm>
#include <cstddef>
#include <ctime>

// Formats HH:MM. False until the server's time arrives, while the year is before
// 2025, and when the text does not fit.
bool FormatAvatarClock(time_t now, char* out, size_t size);

// The status label's width: its text, one pixel of slack so LVGL does not wrap
// on rounding, and a cap that leaves room for the clock and icons.
inline int AvatarBubbleLabelWidth(int text_width, int max_width) {
    return std::max(std::min(text_width + 1, max_width), 1);
}

#endif  // AVATAR_CLOCK_H
