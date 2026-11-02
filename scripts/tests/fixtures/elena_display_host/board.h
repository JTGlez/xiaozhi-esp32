/* Host stub for board.h: controllable battery/network/mute sources. */
#ifndef HOST_BOARD_H
#define HOST_BOARD_H

#include "audio_codec.h"

class Board {
public:
    static Board& GetInstance();
    AudioCodec* GetAudioCodec();
    bool GetBatteryLevel(int& level, bool& charging, bool& discharging);
    const char* GetNetworkStateIcon();
};

/* Host controls */
void host_board_set_battery(int level, bool charging, bool discharging);
void host_board_set_network_icon(const char* icon);

#endif /* HOST_BOARD_H */
