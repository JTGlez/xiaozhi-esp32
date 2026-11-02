#ifndef SCREEN_STREAM_CORE_H
#define SCREEN_STREAM_CORE_H

// The screen stream's pure logic, free of LVGL and ESP-IDF so the host tests can
// run it. The stream sends each distinct piece of screen once, then refers to it
// by hash: Elena's animations loop, so most of what reaches the panel was already
// sent. Every packet is little-endian; tools/screen_viewer.py mirrors this format.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace screen_stream {

struct Area {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    bool operator==(const Area&) const = default;
    bool Covers(const Area& other) const;
};

// Hash of the pixels and of where they go; never zero, which marks an empty entry.
uint32_t HashPixels(const uint8_t* data, size_t size, const Area& area);

// The IPv4 literal in a URL's host, as (a << 24 | b << 16 | c << 8 | d).
std::optional<uint32_t> ParseIpv4Host(std::string_view url);

// A viewer holds the stream only while it keeps renewing.
class Lease {
public:
    explicit Lease(uint32_t duration_ms) : duration_ms_(duration_ms) {}

    // Returns true when this renewal starts a session. An expired lease stays held
    // until Clear(), so a late renewal of the same session does not restart it.
    bool Renew(uint32_t session, uint64_t now_ms);
    bool Active(uint64_t now_ms) const;
    void Clear() { held_ = false; }
    uint32_t session() const { return session_; }

private:
    uint32_t duration_ms_;
    uint32_t session_ = 0;
    uint64_t expires_ms_ = 0;
    bool held_ = false;
};

// Decides which strips still need their pixels sent, within a fixed pool of slots.
// The viewer confirms each strip; until then it stays in a slot and is sent again,
// so a static screen always arrives even over lossy UDP. Strips of the current
// frame are never evicted. Not thread safe: the caller holds a lock.
class StripTracker {
public:
    static constexpr int kNone = -1;

    enum class State : uint8_t { Free, Writing, Ready, Sending };

    struct Slot {
        State state = State::Free;
        bool acked = false;
        bool sent = false;
        uint32_t hash = 0;
        uint32_t frame = 0;
        uint32_t generation = 0;
        uint64_t last_sent_ms = 0;
        Area area;
    };

    StripTracker(size_t slots, size_t known_capacity);

    // Returns the slot to copy the pixels into, or kNone when no copy is needed.
    int Observe(const Area& area, uint32_t hash, uint32_t frame);
    void MarkWritten(int slot);

    // Returns the next slot to send, newest frame first, or kNone.
    int NextToSend(uint64_t now_ms, uint32_t resend_ms);
    void MarkSent(int slot, uint64_t now_ms);
    void Ack(uint32_t hash);
    void Reset();

    const Slot& slot(int index) const { return slots_[index]; }
    uint32_t dropped() const { return dropped_; }

private:
    bool IsKnown(uint32_t hash) const;
    void AddKnown(uint32_t hash);
    void Claim(Slot& slot, const Area& area, uint32_t hash, uint32_t frame);

    std::vector<Slot> slots_;
    std::vector<uint32_t> known_;
    size_t known_count_ = 0;
    uint32_t dropped_ = 0;
};

// What the screen shows now, as hashes in painting order. The stream sends the
// whole map, so a lost map packet heals with the next one.
class ScreenMap {
public:
    static constexpr size_t kMaxEntries = 32;

    struct Entry {
        Area area;
        uint32_t hash = 0;
    };

    void Apply(const Area& area, uint32_t hash);
    void Clear();
    void ClearOverflow() { overflowed_ = false; }

    // True once an entry was lost: only a full redraw makes the map complete again.
    bool overflowed() const { return overflowed_; }
    size_t size() const { return size_; }
    const Entry& entry(size_t index) const { return entries_[index]; }

private:
    std::array<Entry, kMaxEntries> entries_{};
    size_t size_ = 0;
    bool overflowed_ = false;
};

enum class PacketType : uint8_t { Hello = 1, Ack = 2, Chunk = 0x10, Map = 0x11, Stats = 0x12 };
enum class Mode : uint8_t { Strips = 0, Frames = 1 };
enum class Codec : uint8_t { Raw565 = 0, Jpeg = 1 };

inline constexpr size_t kChunkHeaderSize = 30;

// Order of the values in a stats packet. Keep it in step with the viewer.
enum Stat : size_t {
    kStatFramesSeen,
    kStatStripsSeen,
    kStatStripsNew,
    kStatStripsDropped,
    kStatHookUsTotal,
    kStatHookUsMax,
    kStatEncodeCount,
    kStatEncodeUsTotal,
    kStatEncodeUsMax,
    kStatEncodedBytes,
    kStatUdpBytes,
    kStatUdpDatagrams,
    kStatSendErrors,
    kStatPausedLowMemory,
    kStatInternalFree,
    kStatInternalMinFree,
    kStatInternalLargest,
    kStatPsramFree,
    kStatStackHighWater,
    kStatMicFramesDropped,
    kStatAudioDecoded,
    kStatAudioPlayed,
    kStatAudioDropped,
    kStatCount,
};

size_t WriteChunkHeader(uint8_t* out, uint32_t session, uint32_t hash, const Area& area,
                        Codec codec, uint32_t total, uint32_t offset);
// These return the packet's size, or 0 when it does not fit.
size_t WriteMapPacket(uint8_t* out, size_t capacity, uint32_t session, uint32_t sequence,
                      uint32_t time_ms, const ScreenMap& map);
size_t WriteStatsPacket(uint8_t* out, size_t capacity, uint32_t session, const uint32_t* values,
                        size_t count);

// A packet from the viewer. Ack hashes point into the parsed buffer.
struct Control {
    PacketType type = PacketType::Hello;
    uint32_t session = 0;
    Mode mode = Mode::Strips;
    uint8_t quality = 0;  // 0 sends raw RGB565
    uint16_t max_kbps = 0;
    const uint8_t* ack_hashes = nullptr;
    uint16_t ack_count = 0;
};

std::optional<Control> ParseControl(const uint8_t* data, size_t size);
uint32_t ReadAckHash(const Control& control, size_t index);

// Limits the stream's bandwidth, with at most 100 ms of burst.
class Pacer {
public:
    explicit Pacer(uint32_t kbps);
    bool TrySpend(uint64_t now_ms, uint32_t bytes);

private:
    uint64_t bytes_per_ms_x8_;
    uint64_t capacity_x8_;
    uint64_t tokens_x8_;
    uint64_t last_ms_ = 0;
};

}  // namespace screen_stream

#endif  // SCREEN_STREAM_CORE_H
