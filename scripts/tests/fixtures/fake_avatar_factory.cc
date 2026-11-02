#include "fake_avatar_factory.h"

FakeRendererLog& fake_renderer_log() {
    static FakeRendererLog log;
    return log;
}

namespace {
// Overrides only the three methods of the contract: a renderer receives no text.
class FakeRenderer final : public AvatarRenderer {
public:
    void Attach(lv_obj_t* layer) override {
        ++fake_renderer_log().attach_count;
        fake_renderer_log().layer = layer;
    }
    void Present(const AvatarPresentation& presentation) override {
        fake_renderer_log().presentations.push_back(presentation);
    }
    void SetPowerSave(bool on) override { fake_renderer_log().power_save.push_back(on); }
};
}  // namespace

std::unique_ptr<AvatarRenderer> CreateAvatarRenderer(const AvatarArtSource& art) {
    (void)art;
    return std::make_unique<FakeRenderer>();
}
