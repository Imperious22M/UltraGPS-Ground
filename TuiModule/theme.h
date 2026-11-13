//! @file theme.h
//! @brief Palette and small drawing helpers for the ground-station TUI.
//!
//! Look: WarGames WOPR phosphor (green text, amber prompts, block caps),
//! Tron (cyan neon frames and a faint grid on pure black) and the notcurses
//! logo (bold multi-colour gradient blocks). The glyphs are box-drawing and
//! block elements plus a few common single-width symbols (… · ▲ ▼ ▶ ✖), so
//! everything renders inside tmux with any UTF-8 font; cols() and put() rely
//! on every glyph being one column wide.
//!
//! Colours are 0xRRGGBB. frame() and gradientRule() are defined in art.cpp.
//! @note The drawing helpers write to a notcurses plane: call them only from
//!       the TUI thread, which does all the drawing.

#pragma once

#include <notcurses/notcurses.h>
#include <cstdint>
#include <string>

//! @brief Colours and drawing helpers shared by every TUI screen.
namespace theme {

constexpr uint32_t BG        = 0x000000;  //!< Pure black background; ink() always sets it.
constexpr uint32_t PHOSPHOR  = 0x33FF33;  //!< WOPR green, main text.
constexpr uint32_t PHOS_DIM  = 0x1A7F1A;  //!< Dim green: timestamps, placeholders, secondary text.
constexpr uint32_t AMBER     = 0xFFB000;  //!< TX lines, prompts, frame titles.
constexpr uint32_t CYAN      = 0x00E5FF;  //!< Tron frames.
constexpr uint32_t CYAN_DIM  = 0x00606E;  //!< Dim cyan: labels, inactive tabs, key hints.
constexpr uint32_t GRID      = 0x00262D;  //!< Faint background grid (tick separators, graph rules).
constexpr uint32_t ACCENT    = 0xFF7A00;  //!< Tron orange, selection.
constexpr uint32_t ERROR     = 0xFF3355;  //!< Red: errors, LINK OFFLINE, NO ECHO, high jitter.
constexpr uint32_t WHITE     = 0xE8FFE8;  //!< TUI messages ('*' lines) in the log pane.

//! Gradient stops of the notcurses-logo banner (magenta, red, yellow, green,
//! cyan), evenly spaced; also used for the header's gradient rule
constexpr uint32_t LOGO_STOPS[] = { 0xFF00FF, 0xFF3355, 0xFFD000, 0x33FF33, 0x00E5FF };

//! One colour per receiver (R1..R6), shared by the tick panel and the graph
constexpr uint32_t RECV_COLORS[6] = { 0x00E5FF, 0xFF4FD8, 0xFFD000, 0x33FF33, 0xFF7A00, 0x7A8CFF };

//! @brief Linear interpolation between two 0xRRGGBB colours, channel by channel.
//! @param a Colour at t = 0.
//! @param b Colour at t = 1.
//! @param t Position between them, in [0,1].
//! @return The blended colour, each channel rounded to the nearest integer.
inline uint32_t lerp(uint32_t a, uint32_t b, double t){
    //! blend the 8-bit channel at bit offset sh
    auto ch = [&](int sh){
        double va = (a >> sh) & 0xFF, vb = (b >> sh) & 0xFF;
        return static_cast<uint32_t>(va + (vb - va) * t + 0.5) << sh;
    };
    return ch(16) | ch(8) | ch(0);
}

//! @brief Colour at position t along the logo gradient (LOGO_STOPS).
//! @param t Position: 0 is the first stop, 1 the last; values outside [0,1]
//!          are clamped.
//! @return The colour, interpolated between the two nearest stops.
inline uint32_t logoGradient(double t){
    constexpr int n = sizeof(LOGO_STOPS) / sizeof(LOGO_STOPS[0]);
    if(t <= 0) return LOGO_STOPS[0];
    if(t >= 1) return LOGO_STOPS[n - 1];
    //! t falls between stops i and i + 1, (pos - i) of the way along
    double pos = t * (n - 1);
    int i = static_cast<int>(pos);
    return lerp(LOGO_STOPS[i], LOGO_STOPS[i + 1], pos - i);
}

//! @brief Set foreground (and black background) for subsequent output.
//! @param n      Plane to draw on.
//! @param fg     Foreground colour; the background is always BG.
//! @param styles NCSTYLE_* bits; they replace the current styles (none by default).
inline void ink(ncplane* n, uint32_t fg, uint16_t styles = NCSTYLE_NONE){
    ncplane_set_fg_rgb(n, fg);
    ncplane_set_bg_rgb(n, BG);
    ncplane_set_styles(n, styles);
}

//! @brief Number of terminal columns of a UTF-8 string made of single-width glyphs.
//! @param s UTF-8 text.
//! @return The number of code points (bytes that are not UTF-8 continuation bytes).
inline int cols(const std::string& s){
    int c = 0;
    for(unsigned char ch : s) if((ch & 0xC0) != 0x80) ++c;
    return c;
}

//! @brief Print, truncated to maxCols columns (single-width glyphs assumed).
//! @param n       Plane to draw on.
//! @param y       Row.
//! @param x       Column of the first glyph.
//! @param s       UTF-8 text; it is never cut inside a glyph.
//! @param maxCols Columns available (unlimited by default); nothing is drawn
//!                when it is 0 or less.
inline void put(ncplane* n, int y, int x, const std::string& s, int maxCols = 1 << 30){
    if(maxCols <= 0) return;
    if(cols(s) <= maxCols){
        ncplane_putstr_yx(n, y, x, s.c_str());
        return;
    }
    //! Keep the first maxCols glyphs: count lead bytes, copy every byte of
    //! each glyph kept
    std::string cut;
    int c = 0;
    for(size_t i = 0; i < s.size(); ++i){
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if((ch & 0xC0) != 0x80){
            if(c == maxCols) break;
            ++c;
        }
        cut += s[i];
    }
    ncplane_putstr_yx(n, y, x, cut.c_str());
}

//! @brief Double-line Tron frame with an inset amber title "╡ TITLE ╞".
//!
//! Draws the border only; the inside is left as it is. The title is drawn
//! when w > 8, cut to w - 9 columns. Afterwards the ink is PHOSPHOR.
//! @param n     Plane to draw on.
//! @param y,x   Top-left corner.
//! @param h,w   Height and width, border included; nothing is drawn when
//!              either is below 2.
//! @param color Border colour.
//! @param title Title text; no title when empty.
void frame(ncplane* n, int y, int x, int h, int w, uint32_t color,
           const std::string& title = "");

//! @brief Horizontal rule of glyph whose colour sweeps across the logo gradient.
//!
//! Afterwards the ink is PHOSPHOR.
//! @param n     Plane to draw on.
//! @param y,x   Row and first column.
//! @param w     Length in columns.
//! @param glyph Single-width UTF-8 glyph drawn in every column (e.g. "━").
void gradientRule(ncplane* n, int y, int x, int w, const char* glyph);

} // namespace theme
