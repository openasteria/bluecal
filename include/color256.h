// color256.h — конвертация HEX-цвета #003689 в ближайший индекс xterm-256.
// Используется для тёмно-синего / голубого интерфейса календаря.
#pragma once

struct RGB { int r, g, b; };

inline RGB hex_to_rgb(const char* hex) {
    auto comp = [](const char* s) {
        int v = 0;
        for (int i = 0; i < 2 && s[i]; ++i) {
            char c = s[i];
            int d = (c >= '0' && c <= '9') ? (c - '0')
                  : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
                  : (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0;
            v = v * 16 + d;
        }
        return v;
    };
    if (hex[0] == '#') ++hex;
    return RGB{ comp(hex), comp(hex + 2), comp(hex + 4) };
}

// Возвращает ближайший индекс палитры xterm-256 для заданного цвета.
// Учитывает кубическую часть (16..231) и градации серого (232..255).
inline int rgb_to_xterm256(RGB c) {
    int best = 16;
    long bestd = -1L;
    // кубическая часть: 6x6x6
    const int steps[6] = { 0, 95, 135, 175, 215, 255 };
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b) {
                long dr = steps[r] - c.r, dg = steps[g] - c.g, db = steps[b] - c.b;
                long d = dr * dr + dg * dg + db * db;
                if (bestd < 0 || d < bestd) { bestd = d; best = 16 + 36 * r + 6 * g + b; }
            }
    // градации серого 232..255 (8, 18, ..., 238)
    for (int i = 0; i < 24; ++i) {
        int v = 8 + i * 10;
        long dr = v - c.r, dg = v - c.g, db = v - c.b;
        long d = dr * dr + dg * dg + db * db;
        if (d < bestd) { bestd = d; best = 232 + i; }
    }
    return best;
}

inline int hex_to_xterm256(const char* hex) {
    return rgb_to_xterm256(hex_to_rgb(hex));
}
