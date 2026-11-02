#ifndef AVATAR_RENDERER_H
#define AVATAR_RENDERER_H

#include "avatar_controller.h"
#include "emoji_collection.h"

#include <lvgl.h>

#include <memory>

// The art a build gave the avatar.
class AvatarArtSource {
public:
    virtual ~AvatarArtSource() = default;
    // Null until assets load, and it may change when they reload.
    virtual std::shared_ptr<EmojiCollection> Collection() const = 0;
};

// Draws the avatar inside a layer that fills the screen, under the UI. The top
// 30 pixels are reserved for that UI: they may show background, not content that
// matters. A renderer receives no conversation, state or network text.
//
// The caller holds the display lock for every call, so a renderer takes none.
// Attach runs first, once. Present runs only when the presentation changes; it
// must not block or do heavy work. A renderer must always show something: when
// its art fails, it falls back. SetPowerSave(true) shows a sleeping look until
// SetPowerSave(false), without changing the conversation state.
class AvatarRenderer {
public:
    virtual ~AvatarRenderer() = default;
    virtual void Attach(lv_obj_t* layer) = 0;
    virtual void Present(const AvatarPresentation& presentation) = 0;
    virtual void SetPowerSave(bool on) = 0;
};

// Defined by the one renderer a build compiles.
std::unique_ptr<AvatarRenderer> CreateAvatarRenderer(const AvatarArtSource& art);

#endif  // AVATAR_RENDERER_H
