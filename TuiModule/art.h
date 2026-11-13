//! @file art.h
//! @brief Block-letter art for the TUI: the ULTRAGPS banner and the big tick digits.
//!
//! Glyphs are 3 rows tall and built only from "█▀▄ " so they survive tmux and
//! 256-colour terminals. The banner is drawn in the header of every screen,
//! the digits in the MAIN screen's tick box. Defined in art.cpp.

#pragma once

#include <notcurses/notcurses.h>
#include <string>

//! @brief Block-letter banner and digit font.
namespace art {

//! Rows of every glyph (banner letters and digits)
constexpr int GLYPH_ROWS = 3;

//! @brief Width in columns of the banner drawn by drawLogo().
//! @return The letters' columns plus the one-column gaps between them.
int logoWidth();

//! @brief Draw "ULTRAGPS" at (y,x), each column tinted along the logo gradient.
//!
//! phase shifts the gradient (0..1) so the banner can shimmer slowly. The
//! gradient runs out and back (mirrored), so the colours have no seam where
//! it wraps. The gaps between letters are not drawn.
//! @param n     Plane to draw on.
//! @param y,x   Top-left corner; the banner is GLYPH_ROWS rows by logoWidth()
//!              columns.
//! @param phase Gradient shift, 0..1 (the header cycles it every 20 s).
void drawLogo(ncplane* n, int y, int x, double phase);

//! @brief Columns needed to draw digits with drawNumber() (3 per glyph, 1 between).
//! @param digits Text to measure, one glyph per character.
//! @return 4 * digits.size() - 1, or 0 for an empty string.
int numberWidth(const std::string& digits);

//! @brief Draw a decimal string with the 3x3 block font in the current colour.
//!
//! One blank column between glyphs (skipped, not drawn). Characters other
//! than 0-9 are drawn as a dash.
//! @param n      Plane to draw on.
//! @param y,x    Top-left corner of the first glyph.
//! @param digits Characters to draw; numberWidth() gives the width.
void drawNumber(ncplane* n, int y, int x, const std::string& digits);

} // namespace art
