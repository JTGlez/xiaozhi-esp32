/* Host stub for audio_codec.h: only the volume query the status bar uses. */
#ifndef HOST_AUDIO_CODEC_H
#define HOST_AUDIO_CODEC_H

class AudioCodec {
public:
    virtual ~AudioCodec() = default;
    virtual int output_volume();
};

void host_codec_set_volume(int volume);

#endif /* HOST_AUDIO_CODEC_H */
