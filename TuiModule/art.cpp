//! @file art.cpp
//! @brief The block-letter glyphs and the drawing code declared in art.h, plus
//!        theme::frame() and theme::gradientRule() (declared in theme.h).

#include "art.h"
#include "theme.h"

#include <algorithm>

namespace {

//! 3x3 seven-segment style digits, indexed by value: one UTF-8 string per row
const char* const DIGITS[10][art::GLYPH_ROWS] = {
    { "█▀█", "█ █", "▀▀▀" },  // 0
    { " ▀█", "  █", "  ▀" },  // 1
    { "▀▀█", "█▀▀", "▀▀▀" },  // 2
    { "▀▀█", " ▀█", "▀▀▀" },  // 3
    { "█ █", "▀▀█", "  ▀" },  // 4
    { "█▀▀", "▀▀█", "▀▀▀" },  // 5
    { "█▀▀", "█▀█", "▀▀▀" },  // 6
    { "▀▀█", "  █", "  ▀" },  // 7
    { "█▀█", "█▀█", "▀▀▀" },  // 8
    { "█▀█", "▀▀█", "▀▀▀" },  // 9
};
//! Glyph drawn for every character other than 0-9: a bar across the middle row
const char* const DASH[art::GLYPH_ROWS] = { "   ", "▀▀▀", "   " };

//! @brief Banner letters, 4 columns wide: one UTF-8 string per row
struct Letter { const char* rows[art::GLYPH_ROWS]; };
//! The banner, "ULTRAGPS", one Letter per character
const Letter LOGO[] = {
    {{ "█  █", "█  █", "▀▄▄▀" }},  // U
    {{ "█   ", "█   ", "█▄▄▄" }},  // L
    {{ "▀██▀", " ██ ", " ██ " }},  // T
    {{ "█▀▀▄", "█▄▄▀", "█  █" }},  // R
    {{ "▄▀▀▄", "█▄▄█", "█  █" }},  // A
    {{ "▄▀▀▀", "█ ▄▄", "▀▄▄█" }},  // G
    {{ "█▀▀▄", "█▄▄▀", "█   " }},  // P
    {{ "▄▀▀▀", " ▀▀▄", "▄▄▄▀" }},  // S
};
constexpr int LOGO_LETTERS = sizeof(LOGO) / sizeof(LOGO[0]);   //!< Letters in the banner.
constexpr int LETTER_W = 4;     //!< Columns of one banner letter.
constexpr int LETTER_GAP = 1;   //!< Blank columns between two banner letters.

//! @brief Return the UTF-8 glyph at column col of a row string.
//! @param row Row of single-width UTF-8 glyphs.
//! @param col Column, counted from 0.
//! @return The glyph's bytes, or " " when the row is shorter.
std::string glyphAt(const char* row, int col){
    std::string s(row);
    int c = 0;
    for(size_t i = 0; i < s.size();){
        //! length of the UTF-8 sequence, from its lead byte
        size_t len = 1;
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if(ch >= 0xF0) len = 4; else if(ch >= 0xE0) len = 3; else if(ch >= 0xC0) len = 2;
        if(c == col) return s.substr(i, len);
        i += len;
        ++c;
    }
    return " ";
}

} // namespace

namespace art {

int logoWidth(){
    return LOGO_LETTERS * LETTER_W + (LOGO_LETTERS - 1) * LETTER_GAP;
}

void drawLogo(ncplane* n, int y, int x, double phase){
    const int w = logoWidth();
    for(int l = 0; l < LOGO_LETTERS; ++l){
        int lx = l * (LETTER_W + LETTER_GAP);
        for(int c = 0; c < LETTER_W; ++c){
            //! position of this banner column along the gradient, shifted by
            //! phase and wrapped to [0,1)
            double t = (lx + c) / static_cast<double>(w - 1) + phase;
            t -= static_cast<int>(t);  // wrap
            //! mirror so the wrap point has no hard seam
            double tm = t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0;
            theme::ink(n, theme::logoGradient(tm), NCSTYLE_BOLD);
            for(int r = 0; r < GLYPH_ROWS; ++r){
                std::string g = glyphAt(LOGO[l].rows[r], c);
                ncplane_putstr_yx(n, y + r, x + lx + c, g.c_str());
            }
        }
    }
}

int numberWidth(const std::string& digits){
    if(digits.empty()) return 0;
    int n = static_cast<int>(digits.size());
    return n * 3 + (n - 1);
}

void drawNumber(ncplane* n, int y, int x, const std::string& digits){
    int cx = x;
    for(char d : digits){
        const char* const* rows = (d >= '0' && d <= '9') ? DIGITS[d - '0'] : DASH;
        for(int r = 0; r < GLYPH_ROWS; ++r){
            ncplane_putstr_yx(n, y + r, cx, rows[r]);
        }
        //! 3 glyph columns, then the blank column
        cx += 4;
    }
}

} // namespace art

namespace theme {

void frame(ncplane* n, int y, int x, int h, int w, uint32_t color,
           const std::string& title){
    if(h < 2 || w < 2) return;
    ink(n, color);
    std::string top = "╔", bot = "╚";
    for(int i = 0; i < w - 2; ++i){ top += "═"; bot += "═"; }
    top += "╗"; bot += "╝";
    ncplane_putstr_yx(n, y, x, top.c_str());
    ncplane_putstr_yx(n, y + h - 1, x, bot.c_str());
    for(int r = 1; r < h - 1; ++r){
        ncplane_putstr_yx(n, y + r, x, "║");
        ncplane_putstr_yx(n, y + r, x + w - 1, "║");
    }
    //! Title over the top border from column x + 2: "╡ ", the title in amber
    //! (cut to w - 9 columns, so some border remains on the right), " ╞"
    if(!title.empty() && w > 8){
        int tw = std::min(cols(title), w - 9);
        ncplane_putstr_yx(n, y, x + 2, "╡ ");
        ink(n, AMBER, NCSTYLE_BOLD);
        put(n, y, x + 4, title, w - 9);
        ink(n, color);
        ncplane_putstr_yx(n, y, x + 4 + tw, " ╞");
    }
    ink(n, theme::PHOSPHOR);
}

void gradientRule(ncplane* n, int y, int x, int w, const char* glyph){
    for(int i = 0; i < w; ++i){
        ink(n, logoGradient(w > 1 ? i / static_cast<double>(w - 1) : 0.0));
        ncplane_putstr_yx(n, y, x + i, glyph);
    }
    ink(n, PHOSPHOR);
}

} // namespace theme
