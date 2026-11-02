/* Host implementations for the C++ dependency stubs (settings, board,
 * application, fonts, emoji/material lookups, cbin loaders, heap_caps, pm,
 * lcd panel calls). */
#include "application.h"
#include "board.h"
#include "cbin_font.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_pm.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "lvgl.h"
#include "material_symbols.h"
#include "noto_emoji.h"
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

/* ------------------------------------------------------------- Settings */
namespace {
std::map<std::string, std::string>& settings_store() {
    static std::map<std::string, std::string> store;
    return store;
}
}  // namespace

Settings::Settings(const std::string& ns, bool read_only) {
    (void)ns;
    (void)read_only;
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    auto& store = settings_store();
    auto it = store.find(key);
    return it != store.end() ? it->second : default_value;
}

void Settings::SetString(const std::string& key, const std::string& value) {
    settings_store()[key] = value;
}

const char* host_settings_get(const char* ns, const char* key) {
    (void)ns;
    auto& store = settings_store();
    auto it = store.find(key);
    return it != store.end() ? it->second.c_str() : nullptr;
}

/* --------------------------------------------------------------- fonts */
namespace {
lv_font_t make_host_font(uint16_t line_height) {
    lv_font_t font{};
    font.line_height = line_height;
    font.base_line = 3;
    return font;
}
}  // namespace

extern const lv_font_t BUILTIN_TEXT_FONT = make_host_font(14);
extern const lv_font_t BUILTIN_ICON_FONT = make_host_font(14);
extern const lv_font_t font_material_symbols_30_4 = make_host_font(30);
extern const lv_font_t font_noto_emoji_30_4 = make_host_font(30);

/* -------------------------------------------------- emoji/material lookup */
namespace {
unsigned long g_material_utf8_calls = 0;
unsigned long g_noto_utf8_calls = 0;
}  // namespace

const char* material_symbols_get_utf8(const char* name) {
    ++g_material_utf8_calls;
    (void)name;
    /* The shim intentionally has no font glyphs: Elena must never rely on
     * the base font-emoji fallback. */
    return nullptr;
}
unsigned long host_material_utf8_calls(void) { return g_material_utf8_calls; }

const char* noto_emoji_get_utf8(const char* name) {
    ++g_noto_utf8_calls;
    (void)name;
    return nullptr;
}
unsigned long host_noto_utf8_calls(void) { return g_noto_utf8_calls; }

/* ---------------------------------------------------------- cbin loaders */
extern "C" lv_font_t* cbin_font_create(uint8_t* data) {
    (void)data;
    return nullptr;
}
extern "C" void cbin_font_delete(lv_font_t* font) { (void)font; }
extern "C" lv_img_dsc_t* cbin_img_dsc_create(uint8_t* data) {
    (void)data;
    return nullptr;
}
extern "C" void cbin_img_dsc_delete(lv_img_dsc_t* dsc) { (void)dsc; }

/* ------------------------------------------------------------ heap caps */
extern "C" void* heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}
extern "C" void* heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
    (void)caps;
    return calloc(n, size);
}
extern "C" void heap_caps_free(void* ptr) { free(ptr); }
extern "C" size_t heap_caps_get_total_size(uint32_t caps) {
    (void)caps;
    return 0;
}

/* -------------------------------------------------------------- esp_pm */
static int g_pm_dummy = 0;
extern "C" esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type, int arg, const char* name,
                                        esp_pm_lock_handle_t* handle) {
    (void)type;
    (void)arg;
    (void)name;
    *handle = &g_pm_dummy;
    return ESP_OK;
}
extern "C" void esp_pm_lock_delete(esp_pm_lock_handle_t handle) { (void)handle; }
extern "C" esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
extern "C" esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

extern "C" size_t esp_psram_get_size(void) { return 0; }

extern "C" void esp_system_abort(const char* what) {
    fprintf(stderr, "esp_system_abort: %s\n", what);
    abort();
}

/* ------------------------------------------------------------- lcd panel */
extern "C" esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) {
    (void)io;
    return ESP_OK;
}
extern "C" esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x0, int y0, int x1,
                                               int y1, const void* data) {
    (void)panel;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)data;
    return ESP_OK;
}
extern "C" esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on_off) {
    (void)panel;
    (void)on_off;
    return ESP_OK;
}
extern "C" esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel) {
    (void)panel;
    return ESP_OK;
}

/* --------------------------------------------------------------- codec */
namespace {
int g_codec_volume = 70;
class HostCodec : public AudioCodec {
public:
    int output_volume() override { return g_codec_volume; }
};
}  // namespace

void host_codec_set_volume(int volume) { g_codec_volume = volume; }

/* --------------------------------------------------------------- Board */
namespace {
struct BoardState {
    HostCodec codec;
    int battery_level = 80;
    bool charging = false;
    bool discharging = true;
    const char* network_icon = "@net_none";
};
BoardState& board_state() {
    static BoardState state;
    return state;
}
}  // namespace

Board& Board::GetInstance() {
    static Board board;
    return board;
}
AudioCodec* Board::GetAudioCodec() { return &board_state().codec; }
bool Board::GetBatteryLevel(int& level, bool& charging, bool& discharging) {
    level = board_state().battery_level;
    charging = board_state().charging;
    discharging = board_state().discharging;
    return true;
}
const char* Board::GetNetworkStateIcon() { return board_state().network_icon; }

void host_board_set_battery(int level, bool charging, bool discharging) {
    board_state().battery_level = level;
    board_state().charging = charging;
    board_state().discharging = discharging;
}
void host_board_set_network_icon(const char* icon) { board_state().network_icon = icon; }

/* ---------------------------------------------------------- Application */
namespace {
struct AppState {
    DeviceState state = kDeviceStateIdle;
    int play_sound_count = 0;
};
AppState& app_state() {
    static AppState state;
    return state;
}
}  // namespace

Application& Application::GetInstance() {
    static Application app;
    return app;
}
DeviceState Application::GetDeviceState() const { return app_state().state; }
void Application::Schedule(std::function<void()> fn) {
    if (fn) {
        fn();
    }
}
void Application::PlaySound(const char* sound) {
    (void)sound;
    ++app_state().play_sound_count;
}

void host_app_set_device_state(DeviceState state) { app_state().state = state; }
int host_app_play_sound_count(void) { return app_state().play_sound_count; }

int AudioCodec::output_volume() { return 50; }
