// SPDX-License-Identifier: MIT
// Joy-Con 2 BLE report decoding and the existing FPGA 30-byte UART protocol.
// The public protocol references and pinned revisions are listed in README.md.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace j2 {
enum Side : uint8_t { LEFT = 0, RIGHT = 1, UNKNOWN = 2 };
constexpr uint32_t STALE_MS = 200;
constexpr size_t FRAME_BYTES = 30;
constexpr uint32_t LEFT_MASK = (1u << 8) | (1u << 11) | (1u << 13) |
    (0xffu << 16) | (1u << 25);
constexpr uint32_t RIGHT_MASK = 0xffu | (1u << 9) | (1u << 10) |
    (1u << 12) | (1u << 14) | (1u << 24);

struct Report {
    uint32_t buttons;
    uint16_t lx, ly, rx, ry;
};
struct Half {
    Report report{};
    uint32_t last_ms = 0;
    uint32_t reports = 0;
    uint32_t bad_reports = 0;
    bool connected = false;
    bool input_valid = false;
};
struct Merged {
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    uint16_t buttons = 0;
    uint8_t dpad = 0, misc = 0;
    bool left_fresh = false, right_fresh = false;
    uint8_t movement_source = 0; // 0 neutral, 1 left stick, 2 right stick
};

inline uint16_t u16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
        (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint16_t stick_x(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1] & 15u) << 8);
}
inline uint16_t stick_y(const uint8_t* p) {
    return uint16_t(p[1] >> 4) | (uint16_t(p[2]) << 4);
}
inline Side product_side(uint16_t pid) {
    return pid == 0x2067 ? LEFT : pid == 0x2066 ? RIGHT : UNKNOWN;
}
inline Side manufacturer_side(const uint8_t* p, size_t n) {
    // NimBLE includes the two-byte Nintendo company ID (0x0553).
    if (!p || n < 8 || p[0] != 0x53 || p[1] != 0x05 ||
        p[2] != 1 || p[3] != 0 || p[4] != 3 || p[5] != 0x7e) return UNKNOWN;
    // Real pairing advertisements include a state byte after 01 00 03 7E.
    // Example from SDL issue 13189: 53 05 01 00 03 7E 05 66 20 ... (right).
    if (n >= 9) {
        Side side=product_side(u16(p + 7));
        if (side != UNKNOWN) return side;
    }
    // Also accept the compact form used by some public trace adapters.
    return product_side(u16(p + 6));
}
inline bool decode(const uint8_t* p, size_t n, Report& result, bool captured_hidp_prefix=false) {
    if (!p) return false;
    // Native notifications exist in both 62- and 63-byte versions. Their
    // rolling first byte can become 0xA1/0x21, without being a report prefix.
    // Only an explicitly wrapped offline capture may request prefix removal.
    if (captured_hidp_prefix) {
        if ((n!=63 && n!=64) || p[0]!=0xa1) return false;
        ++p;--n;
    }
    if (n != 62 && n != 63) return false;
    result.buttons = u32(p + 4);
    result.lx = stick_x(p + 10); result.ly = stick_y(p + 10);
    result.rx = stick_x(p + 13); result.ry = stick_y(p + 13);
    return true;
}
inline bool missing_pair(uint16_t x, uint16_t y) {
    return (x == 0 || x == 0x7ff) && (y == 0 || y == 0x7ff);
}
inline int16_t normalize(uint16_t raw, bool invert) {
    int32_t value = (int32_t(raw) - 2048) / 4;
    if (invert) value = -value;
    if (value > 511) value = 511;
    if (value < -511) value = -511;
    if (value > -40 && value < 40) value = 0;
    return int16_t(value);
}
inline void axes(const Report& r, Side side, int16_t& x, int16_t& y) {
    uint16_t a = side == LEFT ? r.lx : r.rx;
    uint16_t b = side == LEFT ? r.ly : r.ry;
    if (missing_pair(a, b)) {
        a = side == LEFT ? r.rx : r.lx;
        b = side == LEFT ? r.ry : r.ly;
    }
    if (missing_pair(a, b)) { x = 0; y = 0; return; }
    x = normalize(a, false);
    // Nintendo raw +Y is up; the existing FPGA protocol expects up negative.
    y = normalize(b, true);
}
inline bool fresh(const Half& h, uint32_t now) {
    return h.connected && h.input_valid && uint32_t(now - h.last_ms) < STALE_MS;
}
inline Merged merge(const Half& left, const Half& right, uint32_t now) {
    Merged m;
    m.left_fresh = fresh(left, now);
    m.right_fresh = fresh(right, now);
    uint32_t raw = 0;
    if (m.left_fresh) {
        raw |= left.report.buttons & LEFT_MASK;
        axes(left.report, LEFT, m.lx, m.ly);
    }
    if (m.right_fresh) {
        raw |= right.report.buttons & RIGHT_MASK;
        axes(right.report, RIGHT, m.rx, m.ry);
    }
    // The existing game consumes LX/LY only. Select a whole physical stick,
    // preserving its direction rather than mixing X from one hand with Y from
    // the other. A neutral/disconnected half never steals the active stick.
    int32_t left_power=int32_t(m.lx)*m.lx+int32_t(m.ly)*m.ly;
    int32_t right_power=int32_t(m.rx)*m.rx+int32_t(m.ry)*m.ry;
    if (right_power>left_power) {
        m.lx=m.rx; m.ly=m.ry; m.movement_source=2;
    } else if (left_power) m.movement_source=1;
    // Preserve the printed Nintendo A/B/X/Y labels, rather than Xbox positions.
    // Each half's four face buttons use the same physical positions.
    if (raw & ((1u << 3)|(1u << 18))) m.buttons |= 0x0001; // A / right arrow -> bomb
    if (raw & ((1u << 2)|(1u << 16))) m.buttons |= 0x0002; // B / down arrow -> restart
    if (raw & ((1u << 1)|(1u << 17))) m.buttons |= 0x0004; // X / up arrow -> T test
    if (raw & ((1u << 0)|(1u << 19))) m.buttons |= 0x0008; // Y / left arrow -> CPU/GPU
    if (raw & (1u << 22)) m.buttons |= 0x0010; // L -> focus
    if (raw & (1u << 6)) m.buttons |= 0x0020;  // R -> focus
    if (raw & (1u << 23)) m.buttons |= 0x0040;
    if (raw & (1u << 7)) m.buttons |= 0x0080;
    if (raw & (1u << 12)) m.misc |= 1;
    if (raw & (1u << 8)) m.misc |= 2; // minus -> demo/manual
    if (raw & (1u << 9)) m.misc |= 4; // plus -> pause/resume
    if (raw & (1u << 13)) m.misc |= 8;
    return m;
}
inline void put16(uint8_t* p, int16_t value) {
    p[0] = uint8_t(value); p[1] = uint8_t(uint16_t(value) >> 8);
}
inline void frame(const Merged& m, uint8_t sequence, uint8_t* p) {
    memset(p, 0, FRAME_BYTES);
    p[0] = 0xaa; p[1] = 0x55; p[2] = 1; p[3] = sequence;
    put16(p + 4, m.lx); put16(p + 6, m.ly);
    put16(p + 8, m.rx); put16(p + 10, m.ry);
    put16(p + 12, int16_t(m.buttons)); p[14] = m.dpad; p[15] = m.misc;
    // Unused IMU channels remain zero; this firmware does not claim IMU support.
    for (unsigned i = 2; i < 28; ++i) p[28] = uint8_t(p[28] + p[i]);
    p[29] = 0x0a;
}
} // namespace j2
