// Host harness for the screen stream's pure logic.
// Compiled by scripts/tests/test_screen_stream_core.py with the host compiler.
// Each case is selected by argv[1] so one Python test asserts one behaviour.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "screen_stream_core.h"

using namespace screen_stream;

namespace {

constexpr Area kStrip0{0, 0, 320, 20};
constexpr Area kStrip1{0, 20, 320, 20};
constexpr Area kFull{0, 0, 320, 240};

std::string Hex(const uint8_t* data, size_t size) {
    std::string out;
    char byte[3];
    for (size_t i = 0; i < size; i++) {
        std::snprintf(byte, sizeof(byte), "%02x", data[i]);
        out += byte;
    }
    return out;
}

void TestHashChangesWithOnePixel() {
    std::vector<uint8_t> pixels(12800, 0x55);
    const uint32_t before = HashPixels(pixels.data(), pixels.size(), kStrip0);
    for (size_t i : {size_t{0}, size_t{1}, size_t{6399}, size_t{12799}}) {
        auto changed = pixels;
        changed[i] ^= 0x01;
        assert(HashPixels(changed.data(), changed.size(), kStrip0) != before);
    }
}

void TestHashDependsOnArea() {
    std::vector<uint8_t> pixels(12800, 0x55);
    assert(HashPixels(pixels.data(), pixels.size(), kStrip0) !=
           HashPixels(pixels.data(), pixels.size(), kStrip1));
}

void TestHashIgnoresBufferAlignment() {
    std::mt19937 rng(99);
    alignas(4) uint8_t aligned[1283];
    for (auto& byte : aligned) {
        byte = static_cast<uint8_t>(rng());
    }
    const uint32_t expected = HashPixels(aligned, sizeof(aligned), kStrip0);
    for (size_t start = 1; start < 4; start++) {
        std::vector<uint8_t> shifted(sizeof(aligned) + start);
        std::memcpy(shifted.data() + start, aligned, sizeof(aligned));
        assert(HashPixels(shifted.data() + start, sizeof(aligned), kStrip0) == expected);
    }
}

void TestHashHasNoCollisionsOnRandomStrips() {
    std::mt19937 rng(1234);
    std::set<uint32_t> seen;
    std::vector<uint8_t> pixels(1280);
    for (int i = 0; i < 20000; i++) {
        for (auto& byte : pixels) {
            byte = static_cast<uint8_t>(rng());
        }
        const uint32_t hash = HashPixels(pixels.data(), pixels.size(), kStrip0);
        assert(hash != 0);
        seen.insert(hash);
    }
    assert(seen.size() >= 19999);
}

void TestParseIpv4Host() {
    assert(ParseIpv4Host("http://192.0.2.20:8003/xiaozhi/ota/") == 0xC0000214u);
    assert(ParseIpv4Host("https://198.51.100.1/") == 0xC6336401u);
    assert(ParseIpv4Host("http://198.51.100.1") == 0xC6336401u);
    assert(!ParseIpv4Host("http://robot.local:8003/"));
    assert(!ParseIpv4Host("http://300.0.0.1/"));
    assert(!ParseIpv4Host("http://1.2.3/"));
    assert(!ParseIpv4Host("1.2.3.4"));
    assert(!ParseIpv4Host(""));
}

void TestLeaseExpiresWithoutRenewal() {
    Lease lease(3000);
    assert(!lease.Active(0));
    assert(lease.Renew(7, 1000));
    assert(lease.Active(3999));
    assert(!lease.Active(4000));
}

void TestLeaseReportsNewSessionsOnly() {
    Lease lease(3000);
    assert(lease.Renew(7, 0));
    assert(!lease.Renew(7, 1000));
    assert(lease.Active(3999));
    assert(lease.Renew(8, 1500));
    assert(lease.session() == 8);
    lease.Clear();
    assert(!lease.Active(1500));
    assert(lease.Renew(8, 1500));
}

void TestLeaseLateRenewalDoesNotRestartTheSession() {
    Lease lease(3000);
    assert(lease.Renew(7, 0));
    // A busy task can read the viewer's hello after the lease ran out.
    assert(!lease.Active(5000));
    assert(!lease.Renew(7, 5000));
    assert(lease.Active(5000));
}

void TestTrackerNewStripNeedsCopy() {
    StripTracker tracker(4, 64);
    const int slot = tracker.Observe(kStrip0, 11, 1);
    assert(slot >= 0);
    assert(tracker.slot(slot).hash == 11);
    assert(tracker.slot(slot).area == kStrip0);
    assert(tracker.NextToSend(0, 500) == StripTracker::kNone);
    tracker.MarkWritten(slot);
    assert(tracker.NextToSend(0, 500) == slot);
}

void TestTrackerPendingStripNeedsNoSecondCopy() {
    StripTracker tracker(4, 64);
    const int slot = tracker.Observe(kStrip0, 11, 1);
    tracker.MarkWritten(slot);
    assert(tracker.Observe(kStrip0, 11, 2) == StripTracker::kNone);
    assert(tracker.slot(slot).frame == 2);
}

void TestTrackerAckedStripIsKnown() {
    StripTracker tracker(4, 64);
    const int slot = tracker.Observe(kStrip0, 11, 1);
    tracker.MarkWritten(slot);
    assert(tracker.NextToSend(0, 500) == slot);
    tracker.MarkSent(slot, 0);
    tracker.Ack(11);
    assert(tracker.slot(slot).state == StripTracker::State::Free);
    assert(tracker.Observe(kStrip0, 11, 2) == StripTracker::kNone);
    assert(tracker.NextToSend(9999, 500) == StripTracker::kNone);
}

void TestTrackerEvictsOldestOtherFrame() {
    StripTracker tracker(2, 64);
    const int old_slot = tracker.Observe(kStrip0, 11, 1);
    tracker.MarkWritten(old_slot);
    const int newer_slot = tracker.Observe(kStrip1, 12, 2);
    tracker.MarkWritten(newer_slot);
    const uint32_t generation = tracker.slot(old_slot).generation;
    const int claimed = tracker.Observe(kStrip0, 13, 3);
    assert(claimed == old_slot);
    assert(tracker.slot(claimed).hash == 13);
    assert(tracker.slot(claimed).generation != generation);
    // The evicted strip was never confirmed, so seeing it again copies it again.
    tracker.MarkWritten(claimed);
    assert(tracker.Observe(kStrip0, 11, 4) >= 0);
}

void TestTrackerNeverEvictsCurrentFrame() {
    StripTracker tracker(2, 64);
    tracker.MarkWritten(tracker.Observe(kStrip0, 11, 5));
    tracker.MarkWritten(tracker.Observe(kStrip1, 12, 5));
    assert(tracker.Observe(Area{0, 40, 320, 20}, 13, 5) == StripTracker::kNone);
    assert(tracker.dropped() == 1);
}

void TestTrackerNeverEvictsBusySlots() {
    StripTracker tracker(2, 64);
    const int writing = tracker.Observe(kStrip0, 11, 1);
    const int sending = tracker.Observe(kStrip1, 12, 1);
    tracker.MarkWritten(sending);
    assert(tracker.NextToSend(0, 500) == sending);
    assert(tracker.Observe(Area{0, 40, 320, 20}, 13, 2) == StripTracker::kNone);
    assert(tracker.dropped() == 1);
    assert(tracker.slot(writing).hash == 11);
    assert(tracker.slot(sending).hash == 12);
}

void TestTrackerResendsUnackedStrips() {
    StripTracker tracker(4, 64);
    const int slot = tracker.Observe(kStrip0, 11, 1);
    tracker.MarkWritten(slot);
    assert(tracker.NextToSend(100, 500) == slot);
    tracker.MarkSent(slot, 100);
    assert(tracker.NextToSend(599, 500) == StripTracker::kNone);
    assert(tracker.NextToSend(600, 500) == slot);
}

void TestTrackerSendsNewestFrameFirst() {
    StripTracker tracker(4, 64);
    const int older = tracker.Observe(kStrip0, 11, 1);
    tracker.MarkWritten(older);
    const int newer = tracker.Observe(kStrip1, 12, 2);
    tracker.MarkWritten(newer);
    assert(tracker.NextToSend(0, 500) == newer);
    tracker.MarkSent(newer, 0);
    assert(tracker.NextToSend(0, 500) == older);
}

void TestTrackerAckDuringSendReleasesAfterSend() {
    StripTracker tracker(4, 64);
    const int slot = tracker.Observe(kStrip0, 11, 1);
    tracker.MarkWritten(slot);
    assert(tracker.NextToSend(0, 500) == slot);
    tracker.Ack(11);
    assert(tracker.slot(slot).state == StripTracker::State::Sending);
    tracker.MarkSent(slot, 0);
    assert(tracker.slot(slot).state == StripTracker::State::Free);
}

void TestTrackerForgetsKnownStripsWhenFull() {
    StripTracker tracker(4, 8);
    for (uint32_t hash = 1; hash <= 32; hash++) {
        tracker.Ack(hash);
    }
    int known = 0;
    for (uint32_t hash = 1; hash <= 32; hash++) {
        const int slot = tracker.Observe(kStrip0, hash, hash);
        if (slot == StripTracker::kNone) {
            known++;
        } else {
            tracker.MarkWritten(slot);
            tracker.Ack(hash);
        }
    }
    assert(known < 32);
}

void TestTrackerResetForgetsEverything() {
    StripTracker tracker(4, 64);
    tracker.Ack(11);
    const int slot = tracker.Observe(kStrip1, 12, 1);
    tracker.MarkWritten(slot);
    tracker.Reset();
    assert(tracker.Observe(kStrip0, 11, 1) >= 0);
    assert(tracker.dropped() == 0);
}

void TestMapReplacesCoveredEntries() {
    ScreenMap map;
    map.Apply(kStrip0, 1);
    map.Apply(kStrip1, 2);
    map.Apply(kStrip0, 3);
    assert(map.size() == 2);
    assert(map.entry(0).hash == 2);
    assert(map.entry(1).hash == 3);
    map.Apply(kFull, 4);
    assert(map.size() == 1);
    assert(map.entry(0).hash == 4);
}

void TestMapKeepsPartialOverlayOnTop() {
    ScreenMap map;
    map.Apply(kStrip0, 1);
    const Area label{10, 4, 40, 12};
    map.Apply(label, 2);
    assert(map.size() == 2);
    assert(map.entry(0).hash == 1);
    assert(map.entry(1).hash == 2);
    assert(map.entry(1).area == label);
}

void TestMapReportsOverflow() {
    ScreenMap map;
    for (uint16_t i = 0; i < ScreenMap::kMaxEntries; i++) {
        map.Apply(Area{i, 0, 1, 1}, i + 1u);
    }
    assert(!map.overflowed());
    assert(map.size() == ScreenMap::kMaxEntries);
    map.Apply(Area{100, 100, 1, 1}, 999);
    assert(map.overflowed());
    assert(map.size() == ScreenMap::kMaxEntries);
    assert(map.entry(map.size() - 1).hash == 999);
    map.Apply(kFull, 5);
    assert(!map.overflowed());
    assert(map.size() == 1);
}

void TestChunkHeaderBytes() {
    uint8_t out[kChunkHeaderSize];
    const size_t size = WriteChunkHeader(out, 0x01020304, 0xAABBCCDD, Area{1, 2, 3, 4},
                                         Codec::Jpeg, 0x1000, 0x20);
    assert(size == kChunkHeaderSize);
    const std::string expected =
        "4553" "10" "01" "04030201" "ddccbbaa" "0100" "0200" "0300" "0400" "01" "00"
        "00100000" "20000000";
    assert(Hex(out, size) == expected);
}

void TestMapPacketBytes() {
    ScreenMap map;
    map.Apply(Area{0, 0, 320, 20}, 0x11223344);
    uint8_t out[64];
    const size_t size = WriteMapPacket(out, sizeof(out), 5, 6, 7, map);
    const std::string expected =
        "4553" "11" "01" "05000000" "06000000" "07000000" "0100"
        "0000" "0000" "4001" "1400" "44332211";
    assert(Hex(out, size) == expected);
}

void TestMapPacketRejectsSmallBuffer() {
    ScreenMap map;
    map.Apply(kFull, 1);
    uint8_t out[10];
    assert(WriteMapPacket(out, sizeof(out), 5, 6, 7, map) == 0);
}

void TestStatsPacketBytes() {
    const uint32_t values[2] = {1, 0x0200};
    uint8_t out[32];
    const size_t size = WriteStatsPacket(out, sizeof(out), 9, values, 2);
    const std::string expected = "4553" "12" "01" "09000000" "0200" "01000000" "00020000";
    assert(Hex(out, size) == expected);
}

void TestParseHello() {
    const uint8_t data[] = {'E', 'S', 1, 1, 9, 0, 0, 0, 1, 40, 0xD0, 0x07};
    const auto control = ParseControl(data, sizeof(data));
    assert(control);
    assert(control->type == PacketType::Hello);
    assert(control->session == 9);
    assert(control->mode == Mode::Frames);
    assert(control->quality == 40);
    assert(control->max_kbps == 2000);
}

void TestParseAck() {
    const uint8_t data[] = {'E', 'S', 2, 1, 9, 0, 0, 0, 2, 0, 1, 0, 0, 0, 2, 0, 0, 0};
    const auto control = ParseControl(data, sizeof(data));
    assert(control);
    assert(control->type == PacketType::Ack);
    assert(control->ack_count == 2);
    assert(ReadAckHash(*control, 0) == 1);
    assert(ReadAckHash(*control, 1) == 2);
}

void TestParseRejectsMalformedPackets() {
    const uint8_t bad_magic[] = {'X', 'S', 1, 1, 9, 0, 0, 0, 0, 40, 0, 0};
    assert(!ParseControl(bad_magic, sizeof(bad_magic)));
    const uint8_t bad_version[] = {'E', 'S', 1, 2, 9, 0, 0, 0, 0, 40, 0, 0};
    assert(!ParseControl(bad_version, sizeof(bad_version)));
    const uint8_t short_hello[] = {'E', 'S', 1, 1, 9, 0, 0, 0, 0};
    assert(!ParseControl(short_hello, sizeof(short_hello)));
    const uint8_t bad_mode[] = {'E', 'S', 1, 1, 9, 0, 0, 0, 7, 40, 0, 0};
    assert(!ParseControl(bad_mode, sizeof(bad_mode)));
    const uint8_t lying_ack[] = {'E', 'S', 2, 1, 9, 0, 0, 0, 5, 0, 1, 0, 0, 0};
    assert(!ParseControl(lying_ack, sizeof(lying_ack)));
    const uint8_t robot_packet[] = {'E', 'S', 16, 1, 9, 0, 0, 0, 0, 0, 0, 0};
    assert(!ParseControl(robot_packet, sizeof(robot_packet)));
}

void TestPacerLimitsRate() {
    Pacer pacer(800);  // 100 bytes per millisecond
    assert(pacer.TrySpend(0, 5000));
    assert(pacer.TrySpend(0, 5000));
    assert(!pacer.TrySpend(0, 1));
    assert(!pacer.TrySpend(9, 1000));
    assert(pacer.TrySpend(10, 1000));
}

void TestPacerDoesNotAccumulateIdleTime() {
    Pacer pacer(800);
    assert(pacer.TrySpend(0, 10000));
    assert(pacer.TrySpend(60000, 10000));
    assert(!pacer.TrySpend(60000, 1));
}

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases = {
        {"test_hash_changes_with_one_pixel", TestHashChangesWithOnePixel},
        {"test_hash_depends_on_area", TestHashDependsOnArea},
        {"test_hash_ignores_buffer_alignment", TestHashIgnoresBufferAlignment},
        {"test_hash_has_no_collisions_on_random_strips", TestHashHasNoCollisionsOnRandomStrips},
        {"test_parse_ipv4_host", TestParseIpv4Host},
        {"test_lease_expires_without_renewal", TestLeaseExpiresWithoutRenewal},
        {"test_lease_reports_new_sessions_only", TestLeaseReportsNewSessionsOnly},
        {"test_lease_late_renewal_does_not_restart_the_session", TestLeaseLateRenewalDoesNotRestartTheSession},
        {"test_tracker_new_strip_needs_copy", TestTrackerNewStripNeedsCopy},
        {"test_tracker_pending_strip_needs_no_second_copy", TestTrackerPendingStripNeedsNoSecondCopy},
        {"test_tracker_acked_strip_is_known", TestTrackerAckedStripIsKnown},
        {"test_tracker_evicts_oldest_other_frame", TestTrackerEvictsOldestOtherFrame},
        {"test_tracker_never_evicts_current_frame", TestTrackerNeverEvictsCurrentFrame},
        {"test_tracker_never_evicts_busy_slots", TestTrackerNeverEvictsBusySlots},
        {"test_tracker_resends_unacked_strips", TestTrackerResendsUnackedStrips},
        {"test_tracker_sends_newest_frame_first", TestTrackerSendsNewestFrameFirst},
        {"test_tracker_ack_during_send_releases_after_send", TestTrackerAckDuringSendReleasesAfterSend},
        {"test_tracker_forgets_known_strips_when_full", TestTrackerForgetsKnownStripsWhenFull},
        {"test_tracker_reset_forgets_everything", TestTrackerResetForgetsEverything},
        {"test_map_replaces_covered_entries", TestMapReplacesCoveredEntries},
        {"test_map_keeps_partial_overlay_on_top", TestMapKeepsPartialOverlayOnTop},
        {"test_map_reports_overflow", TestMapReportsOverflow},
        {"test_chunk_header_bytes", TestChunkHeaderBytes},
        {"test_map_packet_bytes", TestMapPacketBytes},
        {"test_map_packet_rejects_small_buffer", TestMapPacketRejectsSmallBuffer},
        {"test_stats_packet_bytes", TestStatsPacketBytes},
        {"test_parse_hello", TestParseHello},
        {"test_parse_ack", TestParseAck},
        {"test_parse_rejects_malformed_packets", TestParseRejectsMalformedPackets},
        {"test_pacer_limits_rate", TestPacerLimitsRate},
        {"test_pacer_does_not_accumulate_idle_time", TestPacerDoesNotAccumulateIdleTime},
    };

    if (argc < 2) {
        std::fprintf(stderr, "usage: %s CASE\n", argv[0]);
        return 2;
    }
    const auto it = cases.find(argv[1]);
    if (it == cases.end()) {
        std::fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 2;
    }
    it->second();
    return 0;
}
