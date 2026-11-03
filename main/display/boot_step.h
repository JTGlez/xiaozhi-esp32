#ifndef BOOT_STEP_H
#define BOOT_STEP_H

// What Application reports while it boots. There is no Wi-Fi step: a display that
// shows these steps starts on it by itself.
enum class BootStep { kFindingServer, kServerFailed, kReady };

#endif  // BOOT_STEP_H
