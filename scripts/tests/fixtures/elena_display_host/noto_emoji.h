/* Host stub for <noto_emoji.h>: the base font-emoji fallback is observable
 * through a call counter but never intended to run for Elena. */
#ifndef HOST_NOTO_EMOJI_H
#define HOST_NOTO_EMOJI_H

#ifdef __cplusplus
extern "C" {
#endif

const char* noto_emoji_get_utf8(const char* name);
unsigned long host_noto_utf8_calls(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_NOTO_EMOJI_H */
