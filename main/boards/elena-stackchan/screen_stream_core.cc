#include "screen_stream_core.h"

#include <algorithm>
#include <cstring>

namespace screen_stream {

namespace {

constexpr uint8_t kMagic0 = 'E';
constexpr uint8_t kMagic1 = 'S';
constexpr uint8_t kVersion = 1;
constexpr size_t kBaseHeaderSize = 8;
constexpr size_t kHelloSize = kBaseHeaderSize + 4;
constexpr size_t kAckHeaderSize = kBaseHeaderSize + 2;
constexpr size_t kMapHeaderSize = kBaseHeaderSize + 10;
constexpr size_t kMapEntrySize = 12;
constexpr size_t kStatsHeaderSize = kBaseHeaderSize + 2;

void PutU16(uint8_t*& out, uint16_t value) {
    *out++ = static_cast<uint8_t>(value);
    *out++ = static_cast<uint8_t>(value >> 8);
}

void PutU32(uint8_t*& out, uint32_t value) {
    PutU16(out, static_cast<uint16_t>(value));
    PutU16(out, static_cast<uint16_t>(value >> 16));
}

void PutHeader(uint8_t*& out, PacketType type, uint32_t session) {
    *out++ = kMagic0;
    *out++ = kMagic1;
    *out++ = static_cast<uint8_t>(type);
    *out++ = kVersion;
    PutU32(out, session);
}

void PutArea(uint8_t*& out, const Area& area) {
    PutU16(out, area.x);
    PutU16(out, area.y);
    PutU16(out, area.w);
    PutU16(out, area.h);
}

uint16_t GetU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t GetU32(const uint8_t* data) {
    return GetU16(data) | (static_cast<uint32_t>(GetU16(data + 2)) << 16);
}

using AliasedWord = uint32_t __attribute__((may_alias));

inline __attribute__((always_inline)) uint32_t MixWord(uint32_t hash, uint32_t word) {
    hash = (hash ^ word) * 0x9E3779B1u;
    return hash ^ (hash >> 15);
}

}  // namespace

bool Area::Covers(const Area& other) const {
    return x <= other.x && y <= other.y && x + w >= other.x + other.w &&
           y + h >= other.y + other.h;
}

// This runs for every pixel on the screen while LVGL holds its lock, and the firmware
// builds for size, so it asks for speed and reads whole words when it can.
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("O2")))
#endif
uint32_t HashPixels(const uint8_t* data, size_t size, const Area& area) {
    // Each step is invertible, so strips that differ in one word cannot collide.
    uint32_t hash = 0x811C9DC5u ^ static_cast<uint32_t>(size);
    hash = MixWord(hash, area.x | (static_cast<uint32_t>(area.y) << 16));
    hash = MixWord(hash, area.w | (static_cast<uint32_t>(area.h) << 16));
    const size_t words = size / 4;
    if (reinterpret_cast<uintptr_t>(data) % 4 == 0) {
        const auto* aligned = reinterpret_cast<const AliasedWord*>(data);
        for (size_t i = 0; i < words; i++) {
            hash = MixWord(hash, aligned[i]);
        }
    } else {
        for (size_t i = 0; i < words; i++) {
            uint32_t word;
            std::memcpy(&word, data + i * 4, sizeof(word));
            hash = MixWord(hash, word);
        }
    }
    if (const size_t rest = size - words * 4; rest != 0) {
        uint32_t tail = 0;
        std::memcpy(&tail, data + words * 4, rest);
        hash = MixWord(hash, tail);
    }
    hash ^= hash >> 16;
    hash *= 0x85EBCA6Bu;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35u;
    hash ^= hash >> 16;
    return hash != 0 ? hash : 1;
}

std::optional<uint32_t> ParseIpv4Host(std::string_view url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rest = url.substr(scheme + 3);
    rest = rest.substr(0, rest.find_first_of(":/"));

    uint32_t address = 0;
    int octets = 0;
    size_t position = 0;
    while (true) {
        const size_t dot = rest.find('.', position);
        const std::string_view part = rest.substr(position, dot - position);
        if (part.empty() || part.size() > 3) {
            return std::nullopt;
        }
        uint32_t value = 0;
        for (char c : part) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            value = value * 10 + static_cast<uint32_t>(c - '0');
        }
        if (value > 255) {
            return std::nullopt;
        }
        address = (address << 8) | value;
        octets++;
        if (dot == std::string_view::npos) {
            break;
        }
        position = dot + 1;
    }
    if (octets != 4) {
        return std::nullopt;
    }
    return address;
}

bool Lease::Renew(uint32_t session, uint64_t now_ms) {
    // Only Clear() ends a held lease: a busy task can read a hello after it ran out.
    const bool started = !held_ || session != session_;
    session_ = session;
    expires_ms_ = now_ms + duration_ms_;
    held_ = true;
    return started;
}

bool Lease::Active(uint64_t now_ms) const {
    return held_ && now_ms < expires_ms_;
}

StripTracker::StripTracker(size_t slots, size_t known_capacity)
    : slots_(slots), known_(known_capacity, 0) {}

bool StripTracker::IsKnown(uint32_t hash) const {
    const size_t capacity = known_.size();
    for (size_t i = 0, index = hash % capacity; i < capacity; i++, index = (index + 1) % capacity) {
        if (known_[index] == hash) {
            return true;
        }
        if (known_[index] == 0) {
            return false;
        }
    }
    return false;
}

void StripTracker::AddKnown(uint32_t hash) {
    if (hash == 0 || IsKnown(hash)) {
        return;
    }
    // Forgetting is safe: a forgotten strip is simply sent again.
    if ((known_count_ + 1) * 4 > known_.size() * 3) {
        std::fill(known_.begin(), known_.end(), 0);
        known_count_ = 0;
    }
    size_t index = hash % known_.size();
    while (known_[index] != 0) {
        index = (index + 1) % known_.size();
    }
    known_[index] = hash;
    known_count_++;
}

void StripTracker::Claim(Slot& slot, const Area& area, uint32_t hash, uint32_t frame) {
    slot.state = State::Writing;
    slot.acked = false;
    slot.sent = false;
    slot.hash = hash;
    slot.frame = frame;
    slot.generation++;
    slot.last_sent_ms = 0;
    slot.area = area;
}

int StripTracker::Observe(const Area& area, uint32_t hash, uint32_t frame) {
    if (IsKnown(hash)) {
        return kNone;
    }
    Slot* free_slot = nullptr;
    Slot* oldest = nullptr;
    for (Slot& slot : slots_) {
        if (slot.state == State::Free) {
            if (free_slot == nullptr) {
                free_slot = &slot;
            }
            continue;
        }
        if (slot.hash == hash) {
            slot.frame = frame;
            return kNone;
        }
        if (slot.state == State::Ready && slot.frame != frame &&
            (oldest == nullptr || slot.frame < oldest->frame)) {
            oldest = &slot;
        }
    }
    Slot* target = free_slot != nullptr ? free_slot : oldest;
    if (target == nullptr) {
        dropped_++;
        return kNone;
    }
    Claim(*target, area, hash, frame);
    return static_cast<int>(target - slots_.data());
}

void StripTracker::MarkWritten(int index) {
    Slot& slot = slots_[index];
    slot.state = slot.acked ? State::Free : State::Ready;
}

int StripTracker::NextToSend(uint64_t now_ms, uint32_t resend_ms) {
    int best = kNone;
    for (size_t i = 0; i < slots_.size(); i++) {
        const Slot& slot = slots_[i];
        if (slot.state != State::Ready || (slot.sent && now_ms - slot.last_sent_ms < resend_ms)) {
            continue;
        }
        if (best == kNone || slot.frame > slots_[best].frame) {
            best = static_cast<int>(i);
        }
    }
    if (best != kNone) {
        slots_[best].state = State::Sending;
    }
    return best;
}

void StripTracker::MarkSent(int index, uint64_t now_ms) {
    Slot& slot = slots_[index];
    slot.sent = true;
    slot.last_sent_ms = now_ms;
    slot.state = slot.acked ? State::Free : State::Ready;
}

void StripTracker::Ack(uint32_t hash) {
    AddKnown(hash);
    for (Slot& slot : slots_) {
        if (slot.state == State::Free || slot.hash != hash) {
            continue;
        }
        if (slot.state == State::Ready) {
            slot.state = State::Free;
        } else {
            slot.acked = true;
        }
    }
}

void StripTracker::Reset() {
    for (Slot& slot : slots_) {
        slot.state = State::Free;
        slot.acked = false;
        slot.sent = false;
        slot.hash = 0;
    }
    std::fill(known_.begin(), known_.end(), 0);
    known_count_ = 0;
    dropped_ = 0;
}

void ScreenMap::Apply(const Area& area, uint32_t hash) {
    size_t kept = 0;
    for (size_t i = 0; i < size_; i++) {
        if (!area.Covers(entries_[i].area)) {
            entries_[kept++] = entries_[i];
        }
    }
    size_ = kept;
    if (size_ == 0) {
        overflowed_ = false;
    }
    if (size_ == kMaxEntries) {
        overflowed_ = true;
        std::copy(entries_.begin() + 1, entries_.end(), entries_.begin());
        size_--;
    }
    entries_[size_++] = Entry{area, hash};
}

void ScreenMap::Clear() {
    size_ = 0;
    overflowed_ = false;
}

size_t WriteChunkHeader(uint8_t* out, uint32_t session, uint32_t hash, const Area& area,
                        Codec codec, uint32_t total, uint32_t offset) {
    uint8_t* cursor = out;
    PutHeader(cursor, PacketType::Chunk, session);
    PutU32(cursor, hash);
    PutArea(cursor, area);
    *cursor++ = static_cast<uint8_t>(codec);
    *cursor++ = 0;
    PutU32(cursor, total);
    PutU32(cursor, offset);
    return static_cast<size_t>(cursor - out);
}

size_t WriteMapPacket(uint8_t* out, size_t capacity, uint32_t session, uint32_t sequence,
                      uint32_t time_ms, const ScreenMap& map) {
    const size_t size = kMapHeaderSize + map.size() * kMapEntrySize;
    if (size > capacity) {
        return 0;
    }
    uint8_t* cursor = out;
    PutHeader(cursor, PacketType::Map, session);
    PutU32(cursor, sequence);
    PutU32(cursor, time_ms);
    PutU16(cursor, static_cast<uint16_t>(map.size()));
    for (size_t i = 0; i < map.size(); i++) {
        PutArea(cursor, map.entry(i).area);
        PutU32(cursor, map.entry(i).hash);
    }
    return size;
}

size_t WriteStatsPacket(uint8_t* out, size_t capacity, uint32_t session, const uint32_t* values,
                        size_t count) {
    const size_t size = kStatsHeaderSize + count * 4;
    if (size > capacity) {
        return 0;
    }
    uint8_t* cursor = out;
    PutHeader(cursor, PacketType::Stats, session);
    PutU16(cursor, static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; i++) {
        PutU32(cursor, values[i]);
    }
    return size;
}

std::optional<Control> ParseControl(const uint8_t* data, size_t size) {
    if (size < kBaseHeaderSize || data[0] != kMagic0 || data[1] != kMagic1 ||
        data[3] != kVersion) {
        return std::nullopt;
    }
    Control control;
    control.session = GetU32(data + 4);
    if (data[2] == static_cast<uint8_t>(PacketType::Hello)) {
        if (size < kHelloSize || data[8] > static_cast<uint8_t>(Mode::Frames)) {
            return std::nullopt;
        }
        control.type = PacketType::Hello;
        control.mode = static_cast<Mode>(data[8]);
        control.quality = data[9];
        control.max_kbps = GetU16(data + 10);
        return control;
    }
    if (data[2] == static_cast<uint8_t>(PacketType::Ack)) {
        if (size < kAckHeaderSize) {
            return std::nullopt;
        }
        control.type = PacketType::Ack;
        control.ack_count = GetU16(data + 8);
        if (size < kAckHeaderSize + size_t{control.ack_count} * 4) {
            return std::nullopt;
        }
        control.ack_hashes = data + kAckHeaderSize;
        return control;
    }
    return std::nullopt;
}

uint32_t ReadAckHash(const Control& control, size_t index) {
    return GetU32(control.ack_hashes + index * 4);
}

Pacer::Pacer(uint32_t kbps)
    : bytes_per_ms_x8_(kbps),
      capacity_x8_(uint64_t{kbps} * 100),
      tokens_x8_(capacity_x8_) {}

bool Pacer::TrySpend(uint64_t now_ms, uint32_t bytes) {
    const uint64_t refill = (now_ms - last_ms_) * bytes_per_ms_x8_;
    tokens_x8_ = std::min(capacity_x8_, tokens_x8_ + refill);
    last_ms_ = now_ms;
    const uint64_t cost = uint64_t{bytes} * 8;
    if (tokens_x8_ < cost) {
        return false;
    }
    tokens_x8_ -= cost;
    return true;
}

}  // namespace screen_stream
