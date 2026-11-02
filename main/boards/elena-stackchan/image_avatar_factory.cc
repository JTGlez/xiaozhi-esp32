#include "image_avatar_renderer.h"

std::unique_ptr<AvatarRenderer> CreateAvatarRenderer(const AvatarArtSource& art) {
    return std::make_unique<ImageAvatarRenderer>(art);
}
