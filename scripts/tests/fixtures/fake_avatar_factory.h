/* A renderer that only records its calls, for the stage's contract tests. */
#ifndef FAKE_AVATAR_FACTORY_H
#define FAKE_AVATAR_FACTORY_H

#include "avatar_renderer.h"

#include <string>
#include <vector>

struct FakeRendererLog {
    int attach_count = 0;
    lv_obj_t* layer = nullptr;
    std::vector<AvatarPresentation> presentations;
    std::vector<bool> power_save;
};

FakeRendererLog& fake_renderer_log();

#endif  // FAKE_AVATAR_FACTORY_H
