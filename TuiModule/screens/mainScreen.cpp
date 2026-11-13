//! @file mainScreen.cpp
//! @brief Tui drawing code for the MAIN screen, and the header drawn on every
//!        screen.
//!
//! The header (rows 0..4) holds the logo, the clock, the serial link and
//! network server status lines, and the screen tabs. The MAIN screen body
//! below it holds the tick readout with each receiver's jitter, one 60 s graph
//! per receiver, the serial chat log and the command line.
//!
//! @note Runs on the TUI thread only (everything is called from Tui::draw()).
//!       It reads a TuiSnapshot, TuiState::chat() and the thread-safe
//!       ServerModule getters, and never touches Comms or the serial port.

#include "../tui.h"
#include "../theme.h"
#include "../art.h"

#include <notcurses/notcurses.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>

namespace {

//! @brief Format a wall-clock time as the local time of day.
//! @param t Time to format.
//! @return "hh:mm:ss" in local time (24-hour clock).
std::string clockString(std::chrono::system_clock::time_point t){
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

//! @brief Blink phase shared by the text cursor and the header banner.
//! @param t0 Start of the blink cycle (Tui::t0_, the TUI start time).
//! @return true for the first 500 ms of every second since @p t0, false for
//!         the other 500 ms.
bool blinkOn(std::chrono::steady_clock::time_point t0){
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    return (ms / 500) % 2 == 0;
}

//! @brief Whether the tick readouts are drawn dimmed: no tick line yet, or
//!        none for 5 s.
//! @param s Snapshot to check.
//! @return true if no tick line has arrived yet, or the last one arrived more
//!         than 5 s ago.
bool ticksStale(const TuiSnapshot& s){
    double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.lastTickTime).count();
    return !s.haveTicks || age > 5.0;
}

//! @brief Jitter colour: green while the receiver's readings are steady,
//!        amber, then red when they move a lot.
//! @param jitter Jitter in ticks (TuiSnapshot::jitter); negative when the
//!               receiver had no non-zero reading in the last
//!               JITTER_SHORT_WINDOW.
//! @return PHOS_DIM for no reading, PHOSPHOR below JITTER_AMBER_TICKS, AMBER
//!         below JITTER_RED_TICKS, ERROR from JITTER_RED_TICKS up.
uint32_t jitterColor(long jitter){
    if(jitter < 0) return theme::PHOS_DIM;
    if(jitter < JITTER_AMBER_TICKS) return theme::PHOSPHOR;
    if(jitter < JITTER_RED_TICKS) return theme::AMBER;
    return theme::ERROR;
}

} // namespace

// ============================================================================
//  Header (drawn on every screen): rows 0..4
// ============================================================================

//! Layout: the ULTRAGPS logo on rows 0..2 at the left. To its right, row 0
//! holds the title and the clock, row 1 the serial link and row 2 the network
//! server. Row 3 is a gradient rule across the full width; row 4 holds the
//! screen tabs on the left and the mode and RX/TX line counters on the right,
//! with a blinking BUSY / BOOTING banner between them.
void Tui::drawHeader(const TuiSnapshot& s, int cols){
    using namespace theme;
    //! the logo's colour shimmer repeats every 20 s
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    art::drawLogo(std_, 0, 2, std::fmod(secs / 20.0, 1.0));

    //! text column: 3 columns right of the logo
    int ix = 2 + art::logoWidth() + 3;
    int iw = cols - ix - 1;
    std::string clock = clockString(std::chrono::system_clock::now());

    //! row 0: title, local clock at the right edge
    ink(std_, PHOSPHOR, NCSTYLE_BOLD);
    put(std_, 0, ix, "GROUND STATION  v1.0", iw - 10);
    ink(std_, AMBER, NCSTYLE_BOLD);
    put(std_, 0, cols - 1 - static_cast<int>(clock.size()), clock);

    //! LINK and CLIENTS are right-aligned and always drawn; the port/IP text
    //! to their left is clipped instead when the terminal is narrow.
    //! Row 1, serial line: port @ baud, then LINK OFFLINE (port closed),
    //! BOOTING <n>s (base-station boot wait) or LINK ONLINE.
    std::string link; uint32_t linkColor;
    if(!s.linkOpen){ link = "LINK OFFLINE"; linkColor = ERROR; }
    else if(s.settleSecondsLeft > 0){ link = "BOOTING " + std::to_string(s.settleSecondsLeft) + "s"; linkColor = AMBER; }
    else { link = "LINK ONLINE"; linkColor = PHOSPHOR; }
    int linkX = cols - 1 - theme::cols(link);
    ink(std_, linkColor, NCSTYLE_BOLD);
    put(std_, 1, linkX, link);
    ink(std_, CYAN_DIM);
    put(std_, 1, ix, "SERIAL", iw);
    ink(std_, PHOSPHOR);
    //! clip the port path (from the left, "…" + its tail), never the baud number
    std::string baud = " @ " + std::to_string(s.baud);
    int serW = std::max(0, linkX - 1 - (ix + 7));
    std::string port = s.serialPort;
    int portW = serW - static_cast<int>(baud.size());
    if(portW < static_cast<int>(port.size()) && portW > 1){
        port = "…" + port.substr(port.size() - (portW - 1));
    }
    put(std_, 1, ix + 7, port + baud, serW);

    //! Row 2, network line: IP and TCP/UDP ports (SERVER DOWN in red when the
    //! server is not running); on the right "CL <TCP clients>/<limit>" ("--"
    //! with the limit off) and "+<n>U" UDP peers, red when more TCP clients
    //! are connected than the limit allows (lowering or enabling the limit
    //! keeps existing sessions).
    size_t tcpN = server_.tcpClientCount();
    std::string net = server_.getIP() + " TCP " + std::to_string(server_.getTcpPort()) +
                      " UDP " + std::to_string(server_.getUdpPort());
    std::string cl = "CL " + std::to_string(tcpN) + "/" +
                     (server_.isClientLimitEnabled() ? std::to_string(server_.getMaxClients()) : std::string("--"));
    std::string clUdp = " +" + std::to_string(server_.udpClientCount()) + "U";
    //! Narrow terminal (e.g. 80 cols): compact "IP T9000 U9001" and drop the
    //! UDP peer count (still on the settings screen) rather than clip the ports.
    if(ix + 7 + theme::cols(net) + 1 + theme::cols(cl + clUdp) >= cols){
        net = server_.getIP() + " T" + std::to_string(server_.getTcpPort()) +
              " U" + std::to_string(server_.getUdpPort());
    } else {
        cl += clUdp;
    }
    int cx = cols - 1 - theme::cols(cl);
    bool over = server_.isClientLimitEnabled() && tcpN > server_.getMaxClients();
    ink(std_, over ? ERROR : AMBER, NCSTYLE_BOLD);
    put(std_, 2, cx, cl);
    ink(std_, CYAN_DIM);
    put(std_, 2, ix, "NET", iw);
    ink(std_, server_.isRunning() ? PHOSPHOR : ERROR);
    put(std_, 2, ix + 4, server_.isRunning() ? net : "SERVER DOWN", std::max(0, cx - 1 - (ix + 4)));

    //! row 3: rule across the full width
    gradientRule(std_, 3, 0, cols, "━");

    //! Row 4, tabs: tab() draws one label at column x, inverse cyan when
    //! active, and returns the column of the next tab
    auto tab = [&](int x, const std::string& label, bool active){
        if(active){
            ncplane_set_fg_rgb(std_, BG);
            ncplane_set_bg_rgb(std_, CYAN);
            ncplane_set_styles(std_, NCSTYLE_BOLD);
        } else {
            ink(std_, CYAN_DIM);
        }
        put(std_, 4, x, label);
        ink(std_, PHOSPHOR);
        return x + theme::cols(label) + 1;
    };
    int tx = tab(1, " F1 MAIN ", screen_ == Screen::Main);
    tx = tab(tx, " F2 SETTINGS ", screen_ == Screen::Settings);
    tx = tab(tx, " F3 HELP ", screen_ == Screen::Help);

    //! right side: mode and RX/TX line counters, left out when they would
    //! run into the tabs
    std::string right = std::string("MODE ") + (s.continuous ? "CONTINUOUS" : "NORMAL") +
                        "  RX " + std::to_string(s.rxCount) + "  TX " + std::to_string(s.txCount);
    //! Blinking banner between the tabs and the counters: the text of a long
    //! operation (delay search) in red, else BASE-STATION BOOTING in amber
    //! during the boot wait; the short form is used when the full one does
    //! not fit
    std::string banner, shortBanner; uint32_t bannerColor = AMBER;
    if(!s.busy.empty()){ banner = s.busy; shortBanner = "BUSY"; bannerColor = ERROR; }
    else if(s.linkOpen && s.settleSecondsLeft > 0){ banner = "BASE-STATION BOOTING"; shortBanner = "BOOTING"; }
    int rx = cols - 1 - theme::cols(right);
    if(rx > tx + 1){
        ink(std_, s.continuous ? PHOSPHOR : PHOS_DIM);
        put(std_, 4, rx, right);
    }
    if(theme::cols(banner) > rx - 2 - (tx + 1)) banner = shortBanner;
    if(!banner.empty() && blinkOn(t0_)){
        int bx = std::max(tx + 1, rx - 2 - theme::cols(banner));
        ink(std_, bannerColor, NCSTYLE_BOLD);
        put(std_, 4, bx, banner, std::max(0, rx - 1 - bx));
    }
}

// ============================================================================
//  MAIN screen body
// ============================================================================

//! Layout below the header: the tick box on rows 5..11, then the graphs on
//! the left and the serial log on the right (45 % of the width, at least 32
//! columns) down to row rows-3, the command line on row rows-2 and the key
//! help on the last row.
void Tui::drawMain(const TuiSnapshot& s, int rows, int cols){
    //! tick box: border, label row, 3 digit rows, jitter row, border
    const int ticksY = 5, ticksH = 7;
    const int lowerY = ticksY + ticksH;
    const int lowerH = rows - 2 - lowerY;
    drawTicks(s, ticksY, 0, ticksH, cols);

    int chatW = std::max(32, cols * 45 / 100);
    int graphW = cols - chatW;
    drawGraph(s, lowerY, 0, lowerH, graphW);
    drawChat(lowerY, graphW, lowerH, chatW);

    drawPrompt(rows - 2, cols, "CMD> ", cmd_);
    //! key help: the long form when it fits, else the short one
    static const std::string KEYS_WIDE =
        "ENTER SEND  HELP OR ? LISTS COMMANDS  UP/DN HISTORY  PGUP/PGDN SCROLL  "
        "ESC CLEAR  TAB NEXT  F3 HELP  F10 QUIT";
    static const std::string KEYS =
        "ENTER SEND  UP/DN HISTORY  PGUP/PGDN SCROLL  ESC CLEAR  F3 HELP  F10 QUIT";
    theme::ink(std_, theme::CYAN_DIM);
    theme::put(std_, rows - 1, 1, theme::cols(KEYS_WIDE) <= cols - 2 ? KEYS_WIDE : KEYS, cols - 2);
}

//! One cell per receiver, cw columns wide including the separator column:
//! label "R1" (y+1), block-digit reading (y+2..y+4), jitter (y+5).
//! The reading shows dashes before the first tick line, NO ECHO for 0, and
//! plain text when the number is too wide for the block font. The label and
//! the reading are dimmed while the ticks are stale (ticksStale()).
void Tui::drawTicks(const TuiSnapshot& s, int y, int x, int h, int w){
    using namespace theme;
    frame(std_, y, x, h, w, CYAN, "TRANSMITTER TICKS");
    int inner = w - 2;
    int cw = inner / static_cast<int>(TUI_RECV_CNT);
    bool stale = ticksStale(s);

    for(size_t i = 0; i < TUI_RECV_CNT; ++i){
        int x0 = x + 1 + static_cast<int>(i) * cw;
        uint32_t col = RECV_COLORS[i];
        if(stale) col = lerp(col, BG, 0.55);

        //! label row: "R1"
        ink(std_, col, NCSTYLE_BOLD);
        put(std_, y + 1, x0 + 1, "R" + std::to_string(i + 1));

        //! jitter row: "JITTER <n>", "JIT <n>" or "<n>", whichever fits,
        //! centred in the cell; "--" while the receiver has no non-zero
        //! reading in the last JITTER_SHORT_WINDOW (jitter < 0). The word is
        //! in a muted receiver colour, the number coloured by jitterColor().
        const long j = s.jitter[i];
        const std::string num = j < 0 ? "--" : std::to_string(j);
        std::string label = "JITTER ";
        if(theme::cols(label + num) > cw - 1) label = "JIT ";
        if(theme::cols(label + num) > cw - 1) label.clear();
        int jx = x0 + std::max(0, (cw - 1 - theme::cols(label + num)) / 2);
        ink(std_, stale ? CYAN_DIM : lerp(col, CYAN_DIM, 0.5));
        put(std_, y + 5, jx, label, cw - 1);
        ink(std_, jitterColor(j), NCSTYLE_BOLD);
        put(std_, y + 5, jx + theme::cols(label), num, cw - 1 - (jx - x0) - theme::cols(label));

        uint32_t v = s.lastTicks[i];
        if(!s.haveTicks){
            //! no data yet: dashes in the block font, as many as fit
            std::string dashes = "----";
            while(dashes.size() > 1 && art::numberWidth(dashes) > cw - 1) dashes.pop_back();
            ink(std_, PHOS_DIM);
            art::drawNumber(std_, y + 2, x0 + std::max(0, (cw - art::numberWidth(dashes)) / 2), dashes);
            continue;
        }
        if(v == 0){
            //! 0 = no echo at this receiver: NO ECHO in red on the middle digit row
            ink(std_, lerp(ERROR, BG, stale ? 0.5 : 0.0), NCSTYLE_BOLD);
            std::string ne = "NO ECHO";
            put(std_, y + 3, x0 + std::max(0, (cw - 7) / 2), ne, cw - 1);
            continue;
        }
        std::string digits = std::to_string(v);
        ink(std_, col, NCSTYLE_BOLD);
        int nw = art::numberWidth(digits);
        if(nw <= cw - 1){
            art::drawNumber(std_, y + 2, x0 + (cw - nw) / 2, digits);
        } else {
            //! too many digits for the block font (3 columns per digit plus 1
            //! between): plain text on the middle digit row
            put(std_, y + 3, x0 + std::max(1, (cw - static_cast<int>(digits.size())) / 2), digits, cw - 1);
        }
    }
    //! faint Tron separators between receivers
    ink(std_, GRID);
    for(size_t i = 1; i < TUI_RECV_CNT; ++i){
        int sx = x + 1 + static_cast<int>(i) * cw - 1;
        for(int r = 1; r < h - 1; ++r) ncplane_putstr_yx(std_, y + r, sx, "│");
    }
    ink(std_, PHOSPHOR);
}

//! Lay the six receivers out as a grid of small graphs. The grid shape
//! (1x6, 2x3, 3x2 or 6x1 columns x rows) is picked per frame from the space
//! available: each cell's plot must be at least 8 columns by 1 row, and among
//! the shapes that fit the one whose plot is "least cramped" wins (width vs
//! height, one row counting as ~6 columns since a time series wants width).
//! If no shape fits, the area shows GRAPH AREA TOO SMALL instead.
void Tui::drawGraph(const TuiSnapshot& s, int y, int x, int h, int w){
    using namespace theme;
    constexpr int N = static_cast<int>(TUI_RECV_CNT);
    //! candidate grid shapes, { columns, rows }
    static const int SHAPES[][2] = { {1, 6}, {2, 3}, {3, 2}, {6, 1} };
    int bestC = 0, bestR = 0, bestScore = -1;
    for(auto& sh : SHAPES){
        int gc = sh[0], gr = sh[1];
        if(gc * gr < N) continue;
        //! plot size inside a cell's frame
        int pw = w / gc - 2, ph = h / gr - 2;
        if(pw < 8 || ph < 1) continue;
        int score = std::min(pw, ph * 6);
        if(score > bestScore){ bestScore = score; bestC = gc; bestR = gr; }
    }
    if(bestScore < 0){
        frame(std_, y, x, h, w, CYAN, "TICKS // 60 S");
        ink(std_, PHOS_DIM, NCSTYLE_BOLD);
        put(std_, y + h / 2, x + 2, "GRAPH AREA TOO SMALL", w - 4);
        ink(std_, PHOSPHOR);
        return;
    }
    //! distribute the remainder so the grid fills the area exactly
    for(int i = 0; i < N; ++i){
        int c = i % bestC, r = i / bestC;
        int cx0 = x + w * c / bestC, cx1 = x + w * (c + 1) / bestC;
        int cy0 = y + h * r / bestR, cy1 = y + h * (r + 1) / bestR;
        drawRecvGraph(s, static_cast<size_t>(i), cy0, cx0, cy1 - cy0, cx1 - cx0);
    }
}

//! One receiver: framed sparkline of the last 60 s (per-second max), its own
//! autoscale (min..max of the non-zero samples), current value in the title,
//! range in the bottom border. Zero (no echo) seconds show as a red dot.
//! Seconds without a tick line are left empty; NO DATA is shown when the
//! whole minute is empty.
void Tui::drawRecvGraph(const TuiSnapshot& s, size_t i, int y, int x, int h, int w){
    using namespace theme;
    const uint32_t col = RECV_COLORS[i];
    bool stale = ticksStale(s);

    std::string title = "R" + std::to_string(i + 1) + " ";
    if(!s.haveTicks) title += "--";
    else if(s.lastTicks[i] == 0) title += "NO ECHO";
    else title += std::to_string(s.lastTicks[i]);
    //! title drawn here rather than by frame(): cells can be ~14 columns wide
    //! and frame() reserves room for its "╡ ╞" brackets
    frame(std_, y, x, h, w, stale ? CYAN_DIM : lerp(col, CYAN, 0.35));
    if(w >= 6){
        ink(std_, stale ? lerp(col, BG, 0.5) : col, NCSTYLE_BOLD);
        put(std_, y, x + 1, " " + title + " ", w - 2);
    }

    const int ix = x + 1, iy = y + 1, pw = w - 2, ph = h - 2;
    if(pw < 1 || ph < 1) return;

    //! Always span the full minute: on narrow cells each column holds `spc`
    //! seconds (their max), on wide cells each second gets `k` columns.
    //! The plot is right-aligned, so the newest second ends at the right border.
    const int seconds = TuiState::historySeconds;
    const int spc = std::max(1, (seconds + pw - 1) / pw);   // seconds per column
    const int ncol = (seconds + spc - 1) / spc;
    const int k = std::max(1, pw / ncol);                   // columns per bucket
    const int px0 = ix + (pw - ncol * k);

    //! Index this receiver's per-second max by second (steady_clock seconds,
    //! the clock TickBucket::sec uses), keeping only the last minute
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count();
    std::map<int64_t, uint32_t> bySec;
    uint32_t lo = UINT32_MAX, hi = 0;   //! range of every non-zero reading shown
    for(auto& b : s.history){
        if(b.sec <= now - seconds) continue;
        bySec[b.sec] = b.max[i];
        if(b.min[i]) lo = std::min(lo, b.min[i]);
        if(b.max[i]) hi = std::max(hi, b.max[i]);
    }

    //! colEnd(c): newest second of plot column c (the last column ends at now)
    auto colEnd = [&](int c){ return now - static_cast<int64_t>(ncol - 1 - c) * spc; };
    //! max of the column's seconds; seen = some second of it had a sample line
    auto bucket = [&](int c, bool& seen) -> uint32_t {
        uint32_t m = 0;
        seen = false;
        for(int d = 0; d < spc; ++d){
            auto it = bySec.find(colEnd(c) - d);
            if(it != bySec.end()){ seen = true; m = std::max(m, it->second); }
        }
        return m;
    };


    //! background grid: a faint vertical rule every 10 s
    ink(std_, GRID);
    for(int c = 0; c < ncol; ++c){
        bool tenth = false;
        for(int d = 0; d < spc; ++d) tenth |= (colEnd(c) - d) % 10 == 0;
        if(!tenth) continue;
        for(int r = 0; r < ph; ++r) ncplane_putstr_yx(std_, iy + r, px0 + c * k, "┊");
    }

    //! bars: block elements from empty to full, in eighths of a row
    static const char* const EIGHTHS[9] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
    for(int c = 0; c < ncol; ++c){
        bool seen;
        uint32_t v = bucket(c, seen);
        if(!seen) continue;
        if(v == 0){
            ink(std_, lerp(ERROR, BG, 0.4));
            for(int cc = 0; cc < k; ++cc) ncplane_putstr_yx(std_, iy + ph - 1, px0 + c * k + cc, "·");
            continue;
        }
        //! bar height: the bottom of the autoscale range (lo) maps to 15 % of
        //! the plot height, the top (hi) to 100 %; a flat range sits at half height
        double frac = hi > lo ? 0.15 + 0.85 * (v - lo) / static_cast<double>(hi - lo) : 0.5;
        int eighths = std::max(1, static_cast<int>(std::lround(frac * ph * 8)));
        for(int r = 0; r < ph; ++r){
            int fill = std::clamp(eighths - r * 8, 0, 8);
            if(fill == 0) continue;
            //! rows fade towards the bottom of the plot; everything is dim when stale
            double shade = ph > 1 ? 0.45 * (1.0 - r / static_cast<double>(ph - 1)) : 0.0;
            if(stale) shade = std::max(shade, 0.5);
            ink(std_, lerp(col, BG, shade));
            for(int cc = 0; cc < k; ++cc){
                ncplane_putstr_yx(std_, iy + ph - 1 - r, px0 + c * k + cc, EIGHTHS[fill]);
            }
        }
    }

    if(bySec.empty()){
        std::string msg = pw >= 12 ? "NO DATA" : "--";
        ink(std_, PHOS_DIM);
        put(std_, iy + (ph - 1) / 2, ix + std::max(0, (pw - theme::cols(msg)) / 2), msg, pw);
    }

    //! bottom border: "-60s" on the left, autoscale range on the right
    const int by = y + h - 1;
    std::string range = hi > 0 ? (hi > lo ? std::to_string(lo) + "-" + std::to_string(hi)
                                          : std::to_string(hi)) : "";
    int rangeX = x + w - 2 - theme::cols(range);
    if(!range.empty() && rangeX > x + 1){
        ink(std_, lerp(col, BG, 0.3));
        put(std_, by, rangeX, range);
    } else {
        rangeX = x + w - 1;
    }
    std::string left = "-" + std::to_string(seconds) + "s";
    if(x + 2 + theme::cols(left) < rangeX - 1){
        ink(std_, CYAN_DIM);
        put(std_, by, x + 2, left);
    }
    ink(std_, PHOSPHOR);
}

//! Each row: "hh:mm:ss", a mark (> sent, < received, * TUI, log level) and the
//! text. TUI lines (help, link notes) wrap to the pane, the others are clipped.
//! The newest lines are at the bottom; chatScroll_ lines are skipped at the
//! end (scrollback).
void Tui::drawChat(int y, int x, int h, int w){
    using namespace theme;
    std::string title = "SERIAL LINK";
    if(chatScroll_ > 0) title += " [-" + std::to_string(chatScroll_) + "]";
    frame(std_, y, x, h, w, CYAN, title);
    const int iy = y + 1, ix = x + 1, ih = h - 2, iw = w - 2;
    const int markX = ix + 9, textX = markX + 2, textW = ix + iw - textX;
    //! pane size, for the PgUp limit and printHelp() (wrap width, scroll position)
    chatPaneRows_ = std::max(1, ih);
    chatTextCols_ = std::max(1, textW);
    if(ih < 1) return;

    //! Lay the newest lines out as rows, then keep the last ih rows.
    //! Row: the chat line it shows, its text (the whole line or one wrapped
    //! part) and whether it is the line's first row (which gets time and mark).
    struct Row { const ChatLine* line; std::string text; bool first; };
    auto lines = state_.chat(static_cast<size_t>(ih), chatScroll_);
    std::vector<Row> rows;
    for(const auto& l : lines){
        if(l.kind == ChatLine::Kind::Sys && theme::cols(l.text) > textW && textW >= 8){
            bool first = true;
            for(auto& part : cmdhelp::wrapText("", l.text, 2, textW)){
                rows.push_back({&l, part, first});
                first = false;
            }
        } else {
            rows.push_back({&l, l.text, true});
        }
    }
    //! rows dropped at the top; fewer rows than the pane sit at its bottom
    const size_t skip = rows.size() > static_cast<size_t>(ih) ? rows.size() - ih : 0;
    //! chat lines (not rows) visible, for the PgUp/PgDn step
    chatLinesShown_ = 0;
    int row = iy + ih - static_cast<int>(rows.size() - skip);
    for(size_t r = skip; r < rows.size(); ++r, ++row){
        const ChatLine& l = *rows[r].line;
        if(r == skip || rows[r].line != rows[r - 1].line) ++chatLinesShown_;
        const char* mark = " ";
        uint32_t col = PHOSPHOR;
        switch(l.kind){
        case ChatLine::Kind::Tx:  mark = ">"; col = AMBER; break;
        case ChatLine::Kind::Rx:  mark = "<"; col = PHOSPHOR; break;
        case ChatLine::Kind::Sys: mark = "*"; col = WHITE; break;
        case ChatLine::Kind::Log:
            //! spdlog levels: 2 info, 3 warn, 4+ error (lower levels look like info)
            mark = l.level >= 4 ? "✖" : l.level == 3 ? "!" : "·";
            col = l.level >= 4 ? ERROR : l.level == 3 ? AMBER : CYAN_DIM;
            break;
        }
        if(rows[r].first){
            ink(std_, PHOS_DIM);
            put(std_, row, ix, clockString(l.t), iw);
            ink(std_, col, NCSTYLE_BOLD);
            put(std_, row, markX, mark, ix + iw - markX);
        }
        ink(std_, col, l.kind == ChatLine::Kind::Tx ? NCSTYLE_BOLD : NCSTYLE_NONE);
        put(std_, row, textX, rows[r].text, textW);
    }
    chatLinesShown_ = std::max(1, chatLinesShown_);
    ink(std_, PHOSPHOR);
}

//! Used for the MAIN command line ("CMD> ") and the SETTINGS value editor
//! ("VALUE> "): amber label at column 1, then the text, scrolled sideways to
//! keep the cursor visible, with a blinking block cursor. Assumes one byte
//! per column (LineEdit only inserts printable ASCII).
void Tui::drawPrompt(int y, int cols, const std::string& label, const LineEdit& ed){
    using namespace theme;
    ink(std_, AMBER, NCSTYLE_BOLD);
    put(std_, y, 1, label);
    int x0 = 1 + theme::cols(label);
    int avail = cols - x0 - 2;
    //! horizontal scroll so the cursor stays visible
    size_t first = ed.cur > static_cast<size_t>(avail) ? ed.cur - avail : 0;
    std::string vis = ed.buf.substr(first, avail);
    ink(std_, PHOSPHOR, NCSTYLE_BOLD);
    put(std_, y, x0, vis);
    if(blinkOn(t0_)){
        //! block cursor: the character under it (or a blank) in inverse video
        int cx = x0 + static_cast<int>(ed.cur - first);
        std::string under = ed.cur < ed.buf.size() ? std::string(1, ed.buf[ed.cur]) : " ";
        ncplane_set_fg_rgb(std_, BG);
        ncplane_set_bg_rgb(std_, PHOSPHOR);
        put(std_, y, cx, under);
    }
    ink(std_, PHOSPHOR);
}
