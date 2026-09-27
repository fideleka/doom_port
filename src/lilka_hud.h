#pragma once

#include <stdint.h>

// Physical-panel HUD. All informative pixels stay inside x=12..267 and
// y<=231, away from the rounded lower corners of Lilka's 280x240 display.
struct LilkaHudState {
    int ammo;
    int health;
    int armor;
    uint16_t owned;
    uint8_t ready;
    uint8_t keys;
};

namespace lilka_hud {
constexpr uint16_t black = 0x0000;
constexpr uint16_t panel = 0x18C3;
constexpr uint16_t slot = 0x2945;
constexpr uint16_t dim = 0x8C71;
constexpr uint16_t white = 0xFFFF;
constexpr uint16_t gold = 0xFDC0;
constexpr uint16_t red = 0xF986;
constexpr uint16_t blue = 0x5D9F;

inline void bar(uint16_t* row, int y, int x, int top, int width, int height,
                uint16_t color) {
    if (y < top || y >= top + height) return;
    for (int i = x; i < x + width; ++i) row[i] = color;
}

// 3x5 uppercase pixel alphabet. Small labels are secondary to the numbers.
inline const uint8_t* glyph(char c) {
    static const uint8_t dash[5] = {0,0,7,0,0};
    static const uint8_t g[][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
        {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1},
        {7,5,7,5,7}, {7,5,7,1,7}, // 0-9
        {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6},
        {7,4,6,4,7}, {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5},
        {7,2,2,2,7}, {1,1,1,5,2}, {5,5,6,5,5}, {4,4,4,4,7},
        {5,7,7,5,5}, {5,7,7,7,5}, {2,5,5,5,2}, {6,5,6,4,4},
        {2,5,5,7,3}, {6,5,6,5,5}, {3,4,2,1,6}, {7,2,2,2,2},
        {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5},
        {5,5,2,2,2}, {7,1,2,4,7} // A-Z
    };
    if (c >= '0' && c <= '9') return g[c - '0'];
    if (c >= 'A' && c <= 'Z') return g[10 + c - 'A'];
    if (c == '-') return dash;
    return nullptr;
}

inline void text(uint16_t* row, int y, int x, int top, const char* s,
                 int scale, uint16_t color) {
    const int gy = (y - top) / scale;
    if (y < top || gy >= 5) return;
    for (int n = 0; s[n]; ++n) {
        const uint8_t* shape = glyph(s[n]);
        if (!shape) continue;
        for (int bit = 0; bit < 3; ++bit) {
            if (shape[gy] & (4 >> bit))
                bar(row, y, x + n * 4 * scale + bit * scale,
                    y, scale, 1, color);
        }
    }
}

inline void number(uint16_t* row, int y, int x, int top, int value,
                   uint16_t color) {
    char digits[5];
    if (value < 0) value = 0;
    if (value > 999) value = 999;
    int length = 0;
    do { digits[length++] = '0' + value % 10; value /= 10; } while (value);
    for (int i = 0; i < length; ++i) {
        char one[] = {digits[length - 1 - i], 0};
        text(row, y, x + i * 12, top, one, 3, color);
    }
}

// Nine recognizable 15x7 silhouettes: fist, saw, pistol, shotgun,
// super shotgun, chaingun, rocket launcher, plasma rifle, BFG.
inline const uint16_t* weaponShape(int index) {
    static const uint16_t shapes[9][7] = {
        {0x0780,0x1FE0,0x3FF0,0x3FF0,0x1FE0,0x0FC0,0x0780},
        {0x0000,0x3FF0,0x7FF8,0x7FF8,0x1FE0,0x0FC0,0x0FC0},
        {0x0000,0x1FE0,0x7FF8,0x7FF8,0x01C0,0x0380,0x0700},
        {0x0000,0x7FFE,0x7FFE,0x1FF0,0x0380,0x0700,0x0E00},
        {0x7FFE,0x7FFE,0x7FFE,0x1FF0,0x0380,0x0700,0x0E00},
        {0x0000,0x7FFE,0x7FFE,0x3FFC,0x07C0,0x0F80,0x1F00},
        {0x0000,0x7FFC,0x7FFE,0x7FFC,0x1FF0,0x0380,0x0700},
        {0x0000,0x3FFC,0x7FFE,0x7FFE,0x1FF0,0x0700,0x0E00},
        {0x0FF0,0x3FFC,0x7FFE,0x7FFE,0x3FFC,0x0FF0,0x0380},
    };
    return shapes[index];
}

inline void renderRow(uint16_t* row, int y, int width,
                      const LilkaHudState& state,
                      const uint32_t* doomFrame) {
    for (int x = 0; x < width; ++x) row[x] = black;
    if (y >= 188 && y < 208) {
        bar(row, y, 12, 188, 256, 20, panel);
        for (int i = 0; i < 9; ++i) {
            const int x = 23 + i * 26;
            const bool selected = state.ready == i;
            const uint16_t ink = !(state.owned & (1 << i)) ? dim
                                : selected ? gold : white;
            bar(row, y, x, 190, 24, 16, selected ? gold : slot);
            bar(row, y, x + 1, 191, 22, 14, panel);
            const int shapeY = y - 194;
            if (shapeY >= 0 && shapeY < 7) {
                const uint16_t bits = weaponShape(i)[shapeY];
                for (int k = 0; k < 15; ++k)
                    if (bits & (1 << (14 - k))) row[x + 4 + k] = ink;
            }
            static const char weaponKeys[] = {'1','1','2','3','3','4','5','6','7'};
            char digit[] = {weaponKeys[i], 0};
            text(row, y, x + 19, 198, digit, 1, ink);
        }
    } else if (y >= 208 && y < 232) {
        bar(row, y, 12, 208, 256, 24, panel);
        text(row, y, 22, 209, "AMMO", 1, dim);
        text(row, y, 83, 209, "HEALTH", 1, dim);
        text(row, y, 176, 209, "ARMOR", 1, dim);
        if (state.ammo < 0) text(row, y, 22, 216, "--", 3, gold);
        else number(row, y, 22, 216, state.ammo, gold);
        number(row, y, 83, 216, state.health, state.health <= 25 ? red : white);
        number(row, y, 176, 216, state.armor, white);
        // The genuine animated Doom face remains at native size. The source
        // status bar is drawn every frame before this physical HUD is composed.
        if (y >= 208 && y < 232) {
            const int fy = 208 + (y - 208) * 29 / 24;
            for (int x = 0; x < 24; ++x) {
                const uint32_t pixel = doomFrame[fy * 320 + 143 + x];
                row[140 + x] = uint16_t(((pixel >> 19) & 0x1f) << 11 |
                                         ((pixel >> 10) & 0x3f) << 5 |
                                         ((pixel >> 3) & 0x1f));
            }
        }
        const uint16_t keyColors[3] = {blue, gold, red};
        for (int i = 0; i < 3; ++i) {
            const int x = 239 + i * 9;
            if (state.keys & (1 << i)) {
                bar(row, y, x, 218, 7, 10, keyColors[i]);
                bar(row, y, x + 2, 220, 3, 3, panel);
            } else {
                bar(row, y, x, 218, 7, 10, dim);
                bar(row, y, x + 1, 219, 5, 8, panel);
            }
        }
    }
}
} // namespace lilka_hud
