#ifndef SCREEN_STREAM_H
#define SCREEN_STREAM_H

#include "sdkconfig.h"

// Sends what reaches the panel to tools/screen_viewer.py over UDP, for debugging.
// Only a viewer on the OTA server's host can request it, and it stops by itself
// when the viewer stops renewing. Call it once the display exists. Without
// CONFIG_ELENA_SCREEN_STREAM it does nothing.
#if CONFIG_ELENA_SCREEN_STREAM
void StartScreenStream();
#else
inline void StartScreenStream() {}
#endif

#endif  // SCREEN_STREAM_H
