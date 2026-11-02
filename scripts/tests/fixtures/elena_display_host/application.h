/* Host stub for application.h: device state and sound scheduling only. */
#ifndef HOST_APPLICATION_H
#define HOST_APPLICATION_H

#include <functional>

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatal,
};

class Application {
public:
    static Application& GetInstance();
    DeviceState GetDeviceState() const;
    void Schedule(std::function<void()> fn);
    void PlaySound(const char* sound);
};

/* Host controls */
void host_app_set_device_state(DeviceState state);
int host_app_play_sound_count(void);

#endif /* HOST_APPLICATION_H */
