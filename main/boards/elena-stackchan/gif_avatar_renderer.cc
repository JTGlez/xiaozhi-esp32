#include "gif_avatar_renderer.h"

#include "elena_avatar_fallback.h"

#include <esp_log.h>

#include <cstring>

static const char* TAG = "GifAvatarRenderer";

namespace {
// Reject malformed containers before handing borrowed data to gifdec, whose
// memory-source API has no length argument. No allocation or decoder is needed.
bool ValidAvatarGif(const lv_img_dsc_t* image) {
    if (!image || !image->data || image->data_size < 14)
        return false;
    const uint8_t* b = image->data;
    const size_t n = image->data_size;
    auto u16 = [&](size_t p) { return unsigned(b[p]) | (unsigned(b[p + 1]) << 8); };
    if ((std::memcmp(b, "GIF89a", 6) && std::memcmp(b, "GIF87a", 6)) || u16(6) != 320 ||
        u16(8) != 240 || !(b[10] & 0x80))
        return false;
    const size_t colors = size_t(2) << (b[10] & 7);
    if (b[11] >= colors)
        return false;
    size_t p = 13 + colors * 3;
    if (p > n)
        return false;
    auto blocks = [&]() {
        while (p < n) {
            size_t length = b[p++];
            if (length == 0)
                return true;
            if (length > n - p)
                return false;
            p += length;
        }
        return false;
    };
    bool frame = false;
    while (p < n) {
        uint8_t type = b[p++];
        if (type == 0x3b)
            return frame && p == n;
        if (type == 0x21) {
            if (p == n)
                return false;
            ++p;  // extension label
            if (!blocks())
                return false;
        } else if (type == 0x2c) {
            if (n - p < 9 || u16(p + 4) == 0 || u16(p + 6) == 0 || u16(p) + u16(p + 4) > 320 ||
                u16(p + 2) + u16(p + 6) > 240)
                return false;
            uint8_t flags = b[p + 8];
            p += 9;
            if (flags & 0x80)
                p += 3 * (size_t(2) << (flags & 7));
            if (p >= n || b[p] < 2 || b[p] > 8)
                return false;
            ++p;
            if (!blocks())
                return false;
            frame = true;
        } else
            return false;
    }
    return false;
}

bool MatchesAvatarIntegrity(const char* name, const lv_img_dsc_t* image) {
    if (!image || !image->data)
        return false;
    for (const auto& entry : elena_avatar_gif_integrity) {
        if (std::strcmp(name, entry.name) != 0 || image->data_size != entry.size)
            continue;
        uint32_t crc = 0xffffffffu;
        for (size_t i = 0; i < image->data_size; ++i) {
            crc ^= image->data[i];
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        return (crc ^ 0xffffffffu) == entry.crc32;
    }
    return false;
}
}  // namespace

GifAvatarRenderer::GifAvatarRenderer(const AvatarArtSource& art) : art_(art) {
    fallback_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    fallback_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    fallback_dsc_.header.w = ELENA_AVATAR_FALLBACK_WIDTH;
    fallback_dsc_.header.h = ELENA_AVATAR_FALLBACK_HEIGHT;
    fallback_dsc_.header.stride = ELENA_AVATAR_FALLBACK_WIDTH * 2;
    fallback_dsc_.data_size = ELENA_AVATAR_FALLBACK_SIZE;
    // Borrowed static storage from the generated header: the fallback path never
    // allocates or copies a frame.
    fallback_dsc_.data = elena_avatar_neutral_rgb565;
}

GifAvatarRenderer::~GifAvatarRenderer() {
    if (gif_) {
        gif_->Stop();
        if (image_ != nullptr) {
            lv_image_set_src(image_, nullptr);
        }
    }
}

void GifAvatarRenderer::Attach(lv_obj_t* layer) {
    image_ = lv_img_create(layer);
    lv_obj_center(image_);
}

void GifAvatarRenderer::Present(const AvatarPresentation& presentation) {
    if (image_ == nullptr) {
        return;
    }
    last_emotion_ = presentation.emotion;
    if (!power_save_) {
        Show(last_emotion_);
    }
}

void GifAvatarRenderer::SetPowerSave(bool on) {
    if (image_ == nullptr) {
        return;
    }
    power_save_ = on;
    Show(on ? "sleeping" : last_emotion_);
}

const LvglImage* GifAvatarRenderer::GetValidGif(const char* emotion) {
    auto collection = art_.Collection();
    if (collection == nullptr) {
        return nullptr;
    }
    const LvglImage* image = collection->GetEmojiImage(emotion);
    if (image == nullptr) {
        return nullptr;
    }
    if (validated_collection_ != collection.get()) {
        validated_collection_ = collection.get();
        validated_count_ = 0;
    }
    const lv_img_dsc_t* dsc = image->image_dsc();
    for (size_t i = 0; i < validated_count_; ++i) {
        if (validated_gifs_[i] == dsc) {
            return image;
        }
    }
    if (!MatchesAvatarIntegrity(emotion, dsc) || !ValidAvatarGif(dsc) || !image->IsGif()) {
        return nullptr;
    }
    if (validated_count_ < validated_gifs_.size()) {
        validated_gifs_[validated_count_++] = dsc;
    }
    return image;
}

void GifAvatarRenderer::ClearExpressionLocked() {
    if (gif_) {
        gif_->Stop();
    }
    lv_image_set_src(image_, nullptr);
    gif_.reset();
    applied_via_gif_ = false;
    applied_identity_ = nullptr;
}

void GifAvatarRenderer::ShowStaticFallbackLocked() {
    if (gif_) {
        gif_->Stop();
        lv_image_set_src(image_, nullptr);
        gif_.reset();
    }
    lv_image_set_src(image_, &fallback_dsc_);
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
    applied_via_gif_ = false;
    applied_identity_ = &fallback_dsc_;
}

bool GifAvatarRenderer::TryExpression(const char* emotion) {
    const LvglImage* image = GetValidGif(emotion);
    if (image == nullptr) {
        return false;
    }
    // Release the previous expression before the replacement allocates its frames.
    ClearExpressionLocked();
    gif_ = std::make_unique<LvglGif>(image->image_dsc());
    if (!gif_->IsLoaded()) {
        // Null on open, decode or allocation failure: not proof of success.
        ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
        gif_.reset();
        return false;
    }
    gif_->SetFrameCallback([this]() { lv_image_set_src(image_, gif_->image_dsc()); });
    lv_image_set_src(image_, gif_->image_dsc());
    gif_->Start();
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
    applied_via_gif_ = true;
    applied_identity_ = gif_->image_dsc();
    return true;
}

void GifAvatarRenderer::RecordApplied(const char* emotion, const EmojiCollection* collection) {
    applied_valid_ = true;
    applied_emotion_ = emotion;
    applied_collection_ = collection;
}

void GifAvatarRenderer::StartGifIfStopped() {
    if (applied_via_gif_ && gif_ != nullptr && !gif_->IsPlaying()) {
        gif_->Start();
    }
}

void GifAvatarRenderer::Show(const char* emotion) {
    auto collection = art_.Collection();
    const EmojiCollection* collection_ptr = collection.get();

    // The same emotion over the same collection instance reopens nothing.
    if (applied_valid_ && std::strcmp(applied_emotion_, emotion) == 0 &&
        applied_collection_ == collection_ptr) {
        StartGifIfStopped();
        return;
    }

    // The target is already on screen, for example a neutral fallback that a
    // later emotion also wants.
    const LvglImage* target = GetValidGif(emotion);
    if (applied_valid_ && target != nullptr && target->image_dsc() == applied_identity_) {
        RecordApplied(emotion, collection_ptr);
        failed_emotion_ = nullptr;
        failed_collection_ = nullptr;
        StartGifIfStopped();
        return;
    }

    if (failed_emotion_ != nullptr && failed_collection_ == collection_ptr &&
        std::strcmp(failed_emotion_, emotion) == 0) {
        ShowStaticFallbackLocked();
        RecordApplied(emotion, collection_ptr);
        return;
    }

    bool applied = TryExpression(emotion);
    if (!applied && std::strcmp(emotion, "neutral") != 0) {
        // Exactly one neutral retry; a failing neutral never retries itself.
        ESP_LOGW(TAG, "Avatar GIF '%s' unavailable, trying neutral fallback", emotion);
        applied = TryExpression("neutral");
    }
    if (applied) {
        RecordApplied(emotion, collection_ptr);
        failed_emotion_ = nullptr;
        failed_collection_ = nullptr;
        return;
    }

    if (collection_ptr == nullptr) {
        ESP_LOGI(TAG, "Assets not loaded yet, showing the static avatar");
    } else {
        ESP_LOGE(TAG, "Avatar assets unavailable, using the static fallback");
    }
    ShowStaticFallbackLocked();
    failed_emotion_ = emotion;
    failed_collection_ = collection_ptr;
    RecordApplied(emotion, collection_ptr);
}
