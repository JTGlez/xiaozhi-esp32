#include "screen_stream.h"

#if CONFIG_ELENA_SCREEN_STREAM

#include "application.h"
#include "jpg/image_to_jpeg.h"
#include "screen_stream_core.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <lwip/sockets.h>
#include <wifi_manager.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>

#define TAG "ScreenStream"

namespace screen_stream {
namespace {

constexpr uint16_t kPort = 12801;
constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr size_t kFrameBytes = kWidth * kHeight * 2;
constexpr size_t kStripBytes = kWidth * 20 * 2;  // The size of LVGL's draw buffer
constexpr size_t kSlots = 24;                    // Twice the strips of a full screen
constexpr size_t kKnownStrips = 1024;
constexpr uint32_t kLeaseMs = 3000;
constexpr uint32_t kResendMs = 500;
constexpr uint32_t kMapRefreshMs = 1000;
constexpr uint32_t kStatsMs = 1000;
constexpr uint32_t kDefaultKbps = 1000;
constexpr size_t kMaxPayload = 1200;
constexpr size_t kMinJpegBytes = 1500;  // Below this, raw pixels are smaller
constexpr size_t kMinInternalFree = 32 * 1024;
constexpr TickType_t kWorkPeriod = pdMS_TO_TICKS(20);
constexpr Area kFullScreen{0, 0, kWidth, kHeight};

uint64_t NowMs() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

void KeepMax(std::atomic<uint32_t>& target, uint32_t value) {
    uint32_t current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

class ScreenStream {
public:
    ScreenStream(lv_display_t* display, uint32_t allowed_host)
        : display_(display), allowed_host_(allowed_host), mutex_(xSemaphoreCreateMutex()) {}

    void Run();

private:
    static void OnFlush(lv_event_t* event);

    // These run in LVGL's task, holding its lock: keep them short.
    void HandleFlush(const lv_area_t& lv_area, const uint8_t* pixels, bool last);
    void FlushStrip(const Area& area, const uint8_t* pixels, size_t size, bool last);
    void FlushFrame(const Area& area, const uint8_t* pixels, bool last);

    void HandlePacket(const uint8_t* data, size_t size, const sockaddr_in& source);
    bool StartStreaming(const Control& hello);
    void StopStreaming();
    void FreeBuffers();
    void Invalidate();

    void DrainSocket();
    void Work();
    void WorkStrips(uint64_t now_ms);
    void WorkFrames(uint64_t now_ms);
    void SendStrip(int slot);
    void SendFrame(uint64_t now_ms);
    void SendMap(uint64_t now_ms);
    void SendStats();

    // Fills `encoded` and returns true when JPEG applies and is smaller.
    bool Encode(const uint8_t* pixels, size_t size, const Area& area, std::string& encoded);
    bool SendChunks(uint32_t hash, const Area& area, Codec codec, const uint8_t* data,
                    size_t size);
    bool SendDatagram(size_t size);

    uint8_t* PixelsOf(int slot) { return slot_pixels_ + static_cast<size_t>(slot) * kStripBytes; }
    void Add(Stat stat, uint32_t value) {
        stats_[stat].fetch_add(value, std::memory_order_relaxed);
    }

    lv_display_t* display_;
    const uint32_t allowed_host_;
    SemaphoreHandle_t mutex_;
    TaskHandle_t task_ = nullptr;
    int socket_ = -1;
    sockaddr_in peer_{};
    std::array<uint8_t, kChunkHeaderSize + kMaxPayload> packet_{};

    Lease lease_{kLeaseMs};
    bool streaming_ = false;
    Mode mode_ = Mode::Strips;
    uint8_t quality_ = 0;
    Pacer pacer_{kDefaultKbps};

    // Shared with the flush callback; guarded by mutex_.
    StripTracker tracker_{kSlots, kKnownStrips};
    ScreenMap map_;
    uint32_t sequence_ = 0;
    bool map_dirty_ = false;

    uint8_t* slot_pixels_ = nullptr;
    std::array<std::string, kSlots> encoded_;
    std::array<uint32_t, kSlots> encoded_generation_{};

    // Frames mode: the callback paints shadow_; the task copies it between frames.
    uint8_t* shadow_ = nullptr;
    uint8_t* frame_copy_ = nullptr;
    std::atomic<uint32_t> shadow_sequence_{0};  // Odd while a frame is being painted
    uint32_t copied_sequence_ = 0;
    std::string frame_encoded_;
    uint32_t frame_hash_ = 0;
    bool frame_pending_ack_ = false;
    uint64_t frame_sent_ms_ = 0;

    uint64_t last_map_ms_ = 0;
    uint64_t last_stats_ms_ = 0;
    std::array<std::atomic<uint32_t>, kStatCount> stats_{};
};

void ScreenStream::OnFlush(lv_event_t* event) {
    auto* self = static_cast<ScreenStream*>(lv_event_get_user_data(event));
    const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
    // This runs before the port swaps the bytes for the panel, so it is plain RGB565.
    const lv_draw_buf_t* buffer = lv_display_get_buf_active(self->display_);
    self->HandleFlush(*area, buffer->data, lv_display_flush_is_last(self->display_));
}

void ScreenStream::HandleFlush(const lv_area_t& lv_area, const uint8_t* pixels, bool last) {
    const int64_t started = esp_timer_get_time();
    const Area area{static_cast<uint16_t>(lv_area.x1), static_cast<uint16_t>(lv_area.y1),
                    static_cast<uint16_t>(lv_area_get_width(&lv_area)),
                    static_cast<uint16_t>(lv_area_get_height(&lv_area))};
    const size_t size = static_cast<size_t>(area.w) * area.h * 2;
    if (size > kStripBytes) {
        Add(kStatStripsDropped, 1);
    } else if (mode_ == Mode::Frames) {
        FlushFrame(area, pixels, last);
    } else {
        FlushStrip(area, pixels, size, last);
    }
    Add(kStatStripsSeen, 1);
    if (last) {
        Add(kStatFramesSeen, 1);
        xTaskNotifyGive(task_);
    }
    const auto elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);
    Add(kStatHookUsTotal, elapsed);
    KeepMax(stats_[kStatHookUsMax], elapsed);
}

void ScreenStream::FlushStrip(const Area& area, const uint8_t* pixels, size_t size, bool last) {
    const uint32_t hash = HashPixels(pixels, size, area);

    xSemaphoreTake(mutex_, portMAX_DELAY);
    map_.Apply(area, hash);
    const int slot = tracker_.Observe(area, hash, sequence_ + 1);
    if (last) {
        sequence_++;
        map_dirty_ = true;
    }
    xSemaphoreGive(mutex_);

    if (slot == StripTracker::kNone) {
        return;
    }
    // Only pixels never seen before pay for this copy.
    std::memcpy(PixelsOf(slot), pixels, size);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    tracker_.MarkWritten(slot);
    xSemaphoreGive(mutex_);
    Add(kStatStripsNew, 1);
}

void ScreenStream::FlushFrame(const Area& area, const uint8_t* pixels, bool last) {
    if ((shadow_sequence_.load(std::memory_order_relaxed) & 1) == 0) {
        shadow_sequence_.fetch_add(1, std::memory_order_release);
    }
    const size_t row_bytes = static_cast<size_t>(area.w) * 2;
    for (int row = 0; row < area.h; row++) {
        std::memcpy(shadow_ + (static_cast<size_t>(area.y + row) * kWidth + area.x) * 2,
                    pixels + row * row_bytes, row_bytes);
    }
    if (last) {
        shadow_sequence_.fetch_add(1, std::memory_order_release);
    }
}

void ScreenStream::Run() {
    task_ = xTaskGetCurrentTaskHandle();
    while (!WifiManager::GetInstance().IsConnected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(kPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (socket_ < 0 || bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        ESP_LOGE(TAG, "Cannot open UDP port %u: %d", kPort, errno);
        return;
    }
    ESP_LOGI(TAG, "Waiting for a viewer on UDP port %u", kPort);

    std::array<uint8_t, 512> buffer;
    while (true) {
        if (!streaming_) {
            // Blocks until a viewer appears, so there is no cost until then.
            sockaddr_in source{};
            socklen_t source_size = sizeof(source);
            const int received =
                recvfrom(socket_, buffer.data(), buffer.size(), 0,
                         reinterpret_cast<sockaddr*>(&source), &source_size);
            if (received > 0) {
                HandlePacket(buffer.data(), received, source);
            } else {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            continue;
        }
        DrainSocket();
        if (!streaming_) {
            continue;
        }
        if (!lease_.Active(NowMs())) {
            StopStreaming();
            continue;
        }
        Work();
        ulTaskNotifyTake(pdTRUE, kWorkPeriod);
    }
}

void ScreenStream::DrainSocket() {
    std::array<uint8_t, 512> buffer;
    while (true) {
        sockaddr_in source{};
        socklen_t source_size = sizeof(source);
        const int received = recvfrom(socket_, buffer.data(), buffer.size(), MSG_DONTWAIT,
                                      reinterpret_cast<sockaddr*>(&source), &source_size);
        if (received <= 0) {
            return;
        }
        HandlePacket(buffer.data(), received, source);
    }
}

void ScreenStream::HandlePacket(const uint8_t* data, size_t size, const sockaddr_in& source) {
    if (ntohl(source.sin_addr.s_addr) != allowed_host_) {
        return;
    }
    const auto control = ParseControl(data, size);
    if (!control) {
        return;
    }
    if (control->type == PacketType::Hello) {
        const bool started = lease_.Renew(control->session, NowMs());
        peer_ = source;
        if (started || !streaming_) {
            if (streaming_) {
                StopStreaming();
                lease_.Renew(control->session, NowMs());
            }
            if (!StartStreaming(*control)) {
                lease_.Clear();
            }
        }
        return;
    }
    if (!streaming_ || control->session != lease_.session()) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < control->ack_count; i++) {
        const uint32_t hash = ReadAckHash(*control, i);
        tracker_.Ack(hash);
        if (hash == frame_hash_) {
            frame_pending_ack_ = false;
        }
    }
    xSemaphoreGive(mutex_);
}

bool ScreenStream::StartStreaming(const Control& hello) {
    mode_ = hello.mode;
    quality_ = hello.quality;
    pacer_ = Pacer(hello.max_kbps != 0 ? hello.max_kbps : kDefaultKbps);

    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    bool allocated;
    if (mode_ == Mode::Strips) {
        slot_pixels_ = static_cast<uint8_t*>(heap_caps_malloc(kSlots * kStripBytes, caps));
        allocated = slot_pixels_ != nullptr;
    } else {
        shadow_ = static_cast<uint8_t*>(heap_caps_calloc(1, kFrameBytes, caps));
        frame_copy_ = static_cast<uint8_t*>(heap_caps_malloc(kFrameBytes, caps));
        allocated = shadow_ != nullptr && frame_copy_ != nullptr;
    }
    if (!allocated) {
        ESP_LOGE(TAG, "Not enough PSRAM for the stream");
        FreeBuffers();
        return false;
    }

    tracker_.Reset();
    map_.Clear();
    sequence_ = 0;
    map_dirty_ = false;
    encoded_generation_.fill(0);
    shadow_sequence_.store(0);
    copied_sequence_ = 0;
    frame_hash_ = 0;
    frame_pending_ack_ = false;
    last_map_ms_ = 0;
    last_stats_ms_ = 0;
    for (auto& stat : stats_) {
        stat.store(0, std::memory_order_relaxed);
    }

    lvgl_port_lock(0);
    lv_display_add_event_cb(display_, OnFlush, LV_EVENT_FLUSH_START, this);
    // A static screen never flushes again, so ask for one full frame to start from.
    lv_obj_invalidate(lv_screen_active());
    lvgl_port_unlock();

    streaming_ = true;
    ESP_LOGI(TAG, "Viewer connected: mode %d, quality %u", static_cast<int>(mode_), quality_);
    return true;
}

void ScreenStream::StopStreaming() {
    // After this the callback cannot run, so its buffers can go.
    lvgl_port_lock(0);
    lv_display_remove_event_cb_with_user_data(display_, OnFlush, this);
    lvgl_port_unlock();
    FreeBuffers();
    streaming_ = false;
    lease_.Clear();
    ESP_LOGI(TAG, "Viewer gone");
}

void ScreenStream::FreeBuffers() {
    heap_caps_free(slot_pixels_);
    heap_caps_free(shadow_);
    heap_caps_free(frame_copy_);
    slot_pixels_ = nullptr;
    shadow_ = nullptr;
    frame_copy_ = nullptr;
    for (auto& encoded : encoded_) {
        std::string().swap(encoded);
    }
    std::string().swap(frame_encoded_);
}

void ScreenStream::Invalidate() {
    lvgl_port_lock(0);
    lv_obj_invalidate(lv_screen_active());
    lvgl_port_unlock();
}

void ScreenStream::Work() {
    const uint64_t now_ms = NowMs();
    if (mode_ == Mode::Strips) {
        WorkStrips(now_ms);
    } else {
        WorkFrames(now_ms);
    }
    SendMap(now_ms);
    if (now_ms - last_stats_ms_ >= kStatsMs) {
        last_stats_ms_ = now_ms;
        SendStats();
    }
}

void ScreenStream::WorkStrips(uint64_t now_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const int slot = tracker_.NextToSend(now_ms, kResendMs);
    xSemaphoreGive(mutex_);
    if (slot != StripTracker::kNone) {
        SendStrip(slot);
    }
}

void ScreenStream::SendStrip(int slot) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const StripTracker::Slot info = tracker_.slot(slot);
    xSemaphoreGive(mutex_);

    // A slot in the Sending state cannot be evicted, so its pixels stay valid.
    const uint8_t* pixels = PixelsOf(slot);
    const size_t size = static_cast<size_t>(info.area.w) * info.area.h * 2;
    std::string& encoded = encoded_[slot];
    if (encoded_generation_[slot] != info.generation) {
        encoded_generation_[slot] = info.generation;
        if (!Encode(pixels, size, info.area, encoded)) {
            encoded.clear();
        }
    }
    if (encoded.empty()) {
        SendChunks(info.hash, info.area, Codec::Raw565, pixels, size);
    } else {
        SendChunks(info.hash, info.area, Codec::Jpeg,
                   reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    tracker_.MarkSent(slot, NowMs());
    stats_[kStatStripsDropped].store(tracker_.dropped(), std::memory_order_relaxed);
    xSemaphoreGive(mutex_);
}

void ScreenStream::WorkFrames(uint64_t now_ms) {
    const uint32_t sequence = shadow_sequence_.load(std::memory_order_acquire);
    if ((sequence & 1) == 0 && sequence != copied_sequence_) {
        std::memcpy(frame_copy_, shadow_, kFrameBytes);
        // A frame that started during the copy tore it; the next pass takes that frame.
        if (shadow_sequence_.load(std::memory_order_acquire) != sequence) {
            return;
        }
        copied_sequence_ = sequence;
        const uint32_t hash = HashPixels(frame_copy_, kFrameBytes, kFullScreen);
        if (!Encode(frame_copy_, kFrameBytes, kFullScreen, frame_encoded_)) {
            frame_encoded_.clear();
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        frame_hash_ = hash;
        map_.Clear();
        map_.Apply(kFullScreen, hash);
        sequence_++;
        map_dirty_ = true;
        xSemaphoreGive(mutex_);
        SendFrame(now_ms);
    } else if (frame_pending_ack_ && now_ms - frame_sent_ms_ >= kResendMs) {
        SendFrame(now_ms);
    }
}

void ScreenStream::SendFrame(uint64_t now_ms) {
    frame_pending_ack_ = true;
    frame_sent_ms_ = now_ms;
    if (frame_encoded_.empty()) {
        SendChunks(frame_hash_, kFullScreen, Codec::Raw565, frame_copy_, kFrameBytes);
    } else {
        SendChunks(frame_hash_, kFullScreen, Codec::Jpeg,
                   reinterpret_cast<const uint8_t*>(frame_encoded_.data()), frame_encoded_.size());
    }
}

void ScreenStream::SendMap(uint64_t now_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool due = map_dirty_ || now_ms - last_map_ms_ >= kMapRefreshMs;
    if (!due) {
        xSemaphoreGive(mutex_);
        return;
    }
    const ScreenMap map = map_;
    const uint32_t sequence = sequence_;
    const bool overflowed = map_.overflowed();
    map_dirty_ = false;
    map_.ClearOverflow();
    xSemaphoreGive(mutex_);

    last_map_ms_ = now_ms;
    const size_t size = WriteMapPacket(packet_.data(), packet_.size(), lease_.session(), sequence,
                                       static_cast<uint32_t>(now_ms), map);
    pacer_.TrySpend(now_ms, size);  // The map always goes: it is what keeps the view current.
    SendDatagram(size);
    if (overflowed) {
        Invalidate();
    }
}

void ScreenStream::SendStats() {
    std::array<uint32_t, kStatCount> values;
    for (size_t i = 0; i < kStatCount; i++) {
        values[i] = stats_[i].load(std::memory_order_relaxed);
    }
    values[kStatHookUsMax] = stats_[kStatHookUsMax].exchange(0, std::memory_order_relaxed);
    values[kStatEncodeUsMax] = stats_[kStatEncodeUsMax].exchange(0, std::memory_order_relaxed);
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    values[kStatInternalFree] = heap_caps_get_free_size(internal);
    values[kStatInternalMinFree] = heap_caps_get_minimum_free_size(internal);
    values[kStatInternalLargest] = heap_caps_get_largest_free_block(internal);
    values[kStatPsramFree] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    values[kStatStackHighWater] = uxTaskGetStackHighWaterMark(nullptr);
    const auto& audio = Application::GetInstance().GetAudioService().debug_statistics();
    values[kStatMicFramesDropped] = audio.encode_drop_count;
    values[kStatAudioDecoded] = audio.decode_count;
    values[kStatAudioPlayed] = audio.playback_count;
    values[kStatAudioDropped] = audio.decode_drop_count;
    SendDatagram(WriteStatsPacket(packet_.data(), packet_.size(), lease_.session(), values.data(),
                                  values.size()));
}

bool ScreenStream::Encode(const uint8_t* pixels, size_t size, const Area& area,
                          std::string& encoded) {
    if (quality_ == 0 || size < kMinJpegBytes || (area.w & 1) != 0) {
        return false;
    }
    encoded.clear();
    const int64_t started = esp_timer_get_time();
    const bool encoded_ok = image_to_jpeg_cb(
        const_cast<uint8_t*>(pixels), size, area.w, area.h, V4L2_PIX_FMT_RGB565, quality_,
        [](void* arg, size_t, const void* data, size_t len) -> size_t {
            if (data != nullptr && len > 0) {
                static_cast<std::string*>(arg)->append(static_cast<const char*>(data), len);
            }
            return len;
        },
        &encoded);
    const auto elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);
    Add(kStatEncodeCount, 1);
    Add(kStatEncodeUsTotal, elapsed);
    KeepMax(stats_[kStatEncodeUsMax], elapsed);
    Add(kStatEncodedBytes, encoded.size());
    return encoded_ok && !encoded.empty() && encoded.size() < size;
}

bool ScreenStream::SendChunks(uint32_t hash, const Area& area, Codec codec, const uint8_t* data,
                              size_t size) {
    for (size_t offset = 0; offset < size; offset += kMaxPayload) {
        // The Wi-Fi driver and the voice share internal RAM: they come first. Maps and
        // stats are small and still go, so a pause can be seen from the viewer.
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < kMinInternalFree) {
            Add(kStatPausedLowMemory, 1);
            return false;
        }
        const size_t payload = std::min(size - offset, kMaxPayload);
        const size_t total = kChunkHeaderSize + payload;
        while (!pacer_.TrySpend(NowMs(), total)) {
            vTaskDelay(1);
        }
        WriteChunkHeader(packet_.data(), lease_.session(), hash, area, codec, size, offset);
        std::memcpy(packet_.data() + kChunkHeaderSize, data + offset, payload);
        if (!SendDatagram(total)) {
            return false;  // Lost chunks are sent again if the viewer does not confirm them.
        }
    }
    return true;
}

bool ScreenStream::SendDatagram(size_t size) {
    const int sent = sendto(socket_, packet_.data(), size, MSG_DONTWAIT,
                            reinterpret_cast<const sockaddr*>(&peer_), sizeof(peer_));
    if (sent < 0) {
        Add(kStatSendErrors, 1);
        return false;
    }
    Add(kStatUdpBytes, static_cast<uint32_t>(sent));
    Add(kStatUdpDatagrams, 1);
    return true;
}

}  // namespace
}  // namespace screen_stream

void StartScreenStream() {
    const auto host = screen_stream::ParseIpv4Host(CONFIG_OTA_URL);
    if (!host) {
        ESP_LOGW(TAG, "The OTA URL has no IPv4 host, so no viewer is allowed");
        return;
    }
    auto* stream = new screen_stream::ScreenStream(lv_display_get_default(), *host);
    // The lowest priority: it only uses what LVGL and the audio tasks leave free.
    xTaskCreatePinnedToCore(
        [](void* arg) {
            static_cast<screen_stream::ScreenStream*>(arg)->Run();
            vTaskDelete(nullptr);
        },
        "screen_stream", 8192, stream, tskIDLE_PRIORITY, nullptr, 1);
}

#endif  // CONFIG_ELENA_SCREEN_STREAM
