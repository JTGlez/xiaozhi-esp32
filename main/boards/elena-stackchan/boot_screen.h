#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#include "boot_view.h"

#include <lvgl.h>

// The screen that covers everything until the robot is ready: the avatar's image
// and name, and the current step in a bubble below them. The caller holds the
// display lock.
class BootScreen {
public:
    BootScreen(lv_obj_t* parent, const lv_image_dsc_t* image, const lv_image_dsc_t* name_image,
               const char* version);
    ~BootScreen();

    BootScreen(const BootScreen&) = delete;
    BootScreen& operator=(const BootScreen&) = delete;

    void Show(const BootView& view);
    void ShowBubble(const char* text, bool is_error);
    // A hint can hold a network's name and a URL, so with text the note replaces
    // the art, and the bubble moves up.
    void SetNote(const char* text);

    lv_obj_t* root() const { return root_; }
    lv_obj_t* image() const { return image_; }
    lv_obj_t* name() const { return name_; }
    lv_obj_t* bubble() const { return bubble_; }
    lv_obj_t* bubble_label() const { return bubble_label_; }
    lv_obj_t* countdown() const { return countdown_; }
    lv_obj_t* note() const { return note_; }
    lv_obj_t* version() const { return version_; }

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* name_ = nullptr;
    lv_obj_t* bubble_ = nullptr;
    lv_obj_t* bubble_label_ = nullptr;
    lv_obj_t* countdown_ = nullptr;
    lv_obj_t* note_ = nullptr;
    lv_obj_t* version_ = nullptr;
    bool has_note_ = false;
};

#endif  // BOOT_SCREEN_H
