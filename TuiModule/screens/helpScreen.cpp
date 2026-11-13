//! @file helpScreen.cpp
//! @brief Tui HELP screen: the command reference (CommsModule/commandHelp),
//!        then the TUI keys and what the MAIN screen readouts mean.
//!
//! Scrolls with Up/Down, PgUp/PgDn and Home/End.
//!
//! @note Runs on the TUI thread only (called from Tui::draw() and the key
//!       handler); it never touches Comms or the serial port.

#include "../tui.h"
#include "../theme.h"

#include <notcurses/notcurses.h>
#include <algorithm>

namespace {

//! @brief Byte offset of a column in a UTF-8 string of single-width glyphs.
//! @param s   UTF-8 string.
//! @param col Column (glyph index, from 0).
//! @return Offset of the glyph at col, or s.size() when s is shorter.
size_t byteAtCol(const std::string& s, int col){
    int c = 0;
    for(size_t i = 0; i < s.size(); ++i){
        if((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) continue;
        if(c == col) return i;
        ++c;
    }
    return s.size();
}

} // namespace

const std::vector<cmdhelp::Line>& Tui::helpLines(int width){
    if(width == helpWidth_ && !helpLines_.empty()) return helpLines_;
    const std::string amber = std::to_string(JITTER_AMBER_TICKS);
    const std::string red = std::to_string(JITTER_RED_TICKS);

    cmdhelp::Writer w(width);
    cmdhelp::appendReference(w);
    w.blank();
    w.title("TUI KEYS");
    w.entry("F1 F2 F3", "MAIN, SETTINGS and HELP screens; Tab goes to the next one.");
    w.entry("F10, Ctrl-Q", "Quit and restore the terminal (Ctrl-C too).");
    w.entry("Ctrl-L", "Redraw the screen.");
    w.entry("MAIN", "Enter sends the command line; Up/Down recall earlier commands; "
                    "PgUp/PgDn scroll the log; Esc clears the line and jumps to the "
                    "newest log line.");
    w.entry("SETTINGS", "Up/Down (or j/k) select; PgUp/PgDn jump 8 rows; Home/End (or "
                        "g/G) go to the first and last row; Enter edits or sends; "
                        "Left/Right cycle a choice; Esc cancels the edit or goes back to "
                        "MAIN; Y confirms R, D, E and M.");
    w.entry("HELP", "Up/Down (or j/k) scroll a line, PgUp/PgDn a page, Home/End go to "
                    "the top and the bottom; Esc goes back to MAIN.");
    w.entry("help / ?", "Typed on the command line or as the RAW COMMAND: prints the "
                        "command reference into the MAIN screen log; nothing is sent.");
    w.blank();
    w.title("MAIN SCREEN READOUTS");
    w.entry("R1 ... R6", "Last tick reading of each receiver in block digits (NO ECHO "
                         "for 0); dimmed when no reading arrived for 5 s.");
    w.entry("JITTER n", "Under each receiver: |mean of its non-zero ticks of the last "
                        "1 s - mean of those of the last 10 s|, in ticks. A high value "
                        "means the readings are moving, i.e. the transmitter is "
                        "changing a lot. Green below " + amber + ", amber below " + red +
                        ", red from " + red + "; -- while either window has no reading.");
    w.entry("Graphs", "Last 60 s of each receiver (per-second maximum), scaled to its "
                      "own range; a red dot marks a second with no echo.");
    w.entry("Log marks", "> sent to the base-station, < received from it, * TUI "
                         "message, ! warning, ✖ error, · other log line.");
    helpLines_ = w.lines();
    helpWidth_ = width;
    return helpLines_;
}

void Tui::drawHelp(int rows, int cols){
    using namespace theme;
    const int y = 5, h = rows - 2 - y;
    frame(std_, y, 0, h, cols, CYAN, "COMMAND REFERENCE");
    const int ix = 2, iw = cols - 4, ih = std::max(1, h - 2);
    //! at most 100 columns of text: long lines are hard to read
    const auto& lines = helpLines(std::min(iw, 100));
    const int n = static_cast<int>(lines.size());
    helpPage_ = ih;
    helpScroll_ = std::clamp(helpScroll_, 0, std::max(0, n - ih));

    //! scroll hints
    if(helpScroll_ > 0){ ink(std_, CYAN_DIM); put(std_, y, cols - 5, "▲"); }
    if(helpScroll_ + ih < n){ ink(std_, CYAN_DIM); put(std_, y + h - 1, cols - 5, "▼"); }

    for(int r = 0; r < ih && helpScroll_ + r < n; ++r){
        const cmdhelp::Line& l = lines[static_cast<size_t>(helpScroll_ + r)];
        const int ry = y + 1 + r;
        //! key part (command syntax or detail label), then the rest
        const size_t split = byteAtCol(l.text, l.keyCols);
        const std::string key = l.text.substr(0, split), rest = l.text.substr(split);
        switch(l.kind){
        case cmdhelp::Line::Kind::Title:
            ink(std_, AMBER, NCSTYLE_BOLD);
            put(std_, ry, ix, l.text, iw);
            break;
        case cmdhelp::Line::Kind::Text:
            ink(std_, PHOSPHOR);
            put(std_, ry, ix, l.text, iw);
            break;
        case cmdhelp::Line::Kind::Command:
            ink(std_, ACCENT, NCSTYLE_BOLD);
            put(std_, ry, ix, key, iw);
            ink(std_, PHOSPHOR, NCSTYLE_BOLD);
            put(std_, ry, ix + l.keyCols, rest, iw - l.keyCols);
            break;
        case cmdhelp::Line::Kind::Detail:
            ink(std_, CYAN_DIM);
            put(std_, ry, ix, key, iw);
            ink(std_, CYAN);
            put(std_, ry, ix + l.keyCols, rest, iw - l.keyCols);
            break;
        }
    }

    //! position and a tip (shortened to fit), then the key hints
    const int last = std::min(n, helpScroll_ + ih);
    std::string status = "LINES " + std::to_string(n ? helpScroll_ + 1 : 0) + "-" +
                         std::to_string(last) + " OF " + std::to_string(n) + "   ";
    std::string tip = "HELP OR ? ON THE COMMAND LINE PRINTS THE COMMANDS INTO THE LOG";
    if(theme::cols(status + tip) > cols - 2) tip = "HELP OR ? PRINTS THE COMMANDS INTO THE LOG";
    ink(std_, PHOS_DIM);
    put(std_, rows - 2, 1, status + tip, cols - 2);
    ink(std_, CYAN_DIM);
    put(std_, rows - 1, 1,
        "UP/DN LINE  PGUP/PGDN PAGE  HOME/END TOP/BOTTOM  ESC MAIN  TAB NEXT  F10 QUIT",
        cols - 2);
    ink(std_, PHOSPHOR);
}

void Tui::handleHelpKey(const ncinput& ni){
    const uint32_t id = ni.id;
    const int page = std::max(1, helpPage_ - 1);
    if(id == NCKEY_UP || id == 'k') --helpScroll_;
    else if(id == NCKEY_DOWN || id == 'j') ++helpScroll_;
    else if(id == NCKEY_PGUP) helpScroll_ -= page;
    else if(id == NCKEY_PGDOWN || id == ' ') helpScroll_ += page;
    else if(id == NCKEY_HOME || id == 'g') helpScroll_ = 0;
    else if(id == NCKEY_END || id == 'G') helpScroll_ = static_cast<int>(helpLines_.size());
    else if(id == NCKEY_ESC) screen_ = Screen::Main;
    //! keep within the text laid out by the last draw
    helpScroll_ = std::clamp(helpScroll_, 0,
                             std::max(0, static_cast<int>(helpLines_.size()) - helpPage_));
}
