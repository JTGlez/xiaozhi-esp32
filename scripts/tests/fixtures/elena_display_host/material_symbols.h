/* Host stub for <material_symbols.h>: icon codepoints become distinct ASCII
 * markers so tests can see which icon was set, and the utf8 lookup used by
 * the base font-emoji fallback is observable through a call counter. */
#ifndef HOST_MATERIAL_SYMBOLS_H
#define HOST_MATERIAL_SYMBOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MATERIAL_SYMBOLS_ROBOT_2 "@robot2"
#define MATERIAL_SYMBOLS_VOLUME_OFF "@vol_off"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT "@bat_bolt"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_0 "@bat_0"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_1 "@bat_1"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_2 "@bat_2"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_3 "@bat_3"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_4 "@bat_4"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_5 "@bat_5"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_6 "@bat_6"
#define MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL "@bat_full"

const char* material_symbols_get_utf8(const char* name);
unsigned long host_material_utf8_calls(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_MATERIAL_SYMBOLS_H */
