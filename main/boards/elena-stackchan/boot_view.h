#ifndef BOOT_VIEW_H
#define BOOT_VIEW_H

#include "boot_step.h"

#include <string>

// What the boot screen says at each step. No LVGL, so host tests can run it.
struct BootView {
    const char* bubble;
    bool is_error;
    int retry_seconds;
};

BootView InitialBootView();
BootView ResolveBootView(BootStep step, int retry_seconds);
std::string BootCountdownText(int retry_seconds);

#endif  // BOOT_VIEW_H
