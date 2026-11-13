//! @file tui.cpp
//! @brief Tui core: starting and stopping notcurses, restoring the terminal
//!        after a crash, the UI thread's input/render loop, the keys of every
//!        screen and of MAIN, and the command line.
//!
//! The screens are drawn in screens/mainScreen.cpp, screens/settingsScreen.cpp
//! and screens/helpScreen.cpp. The API is documented in tui.h.

#include "tui.h"
#include "theme.h"

#include <notcurses/notcurses.h>
#include <algorithm>
#include <cctype>
#include <clocale>
#include <ctime>
#include <csignal>
#include <exception>
#include <termios.h>
#include <unistd.h>

namespace {
//! Terminal state saved before notcurses takes over, for the crash path
struct termios g_savedTermios;
//! 1 if g_savedTermios holds the terminal state (tcgetattr() succeeded)
volatile sig_atomic_t g_haveTermios = 0;
//! 1 while notcurses owns the terminal; cleared by Tui::stop() and by the first
//! emergencyRestore(), so the restore runs at most once
volatile sig_atomic_t g_tuiActive = 0;

//! @brief Async-signal-safe restore: cooked mode, leave the alternate screen, show
//!        the cursor, reset colors. Used when the normal stop() path cannot run.
//!
//! Does nothing unless the TUI owns the terminal (g_tuiActive).
void emergencyRestore(){
    if(!g_tuiActive) return;
    g_tuiActive = 0;
    //! SGR reset, show the cursor, leave the alternate screen, new line
    static const char seq[] = "\x1b[0m\x1b[?25h\x1b[?1049l\r\n";
    ssize_t r = write(STDOUT_FILENO, seq, sizeof(seq) - 1);
    (void)r;
    if(g_haveTermios) tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTermios);
}

//! @brief SIGSEGV/SIGABRT/SIGILL/SIGBUS/SIGFPE: restore, then die with the default action.
//! @param sig The signal received; it is raised again with its default action.
void onFatalSignal(int sig){
    emergencyRestore();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
} // namespace

void Tui::installCrashHandlers(){
    for(int sig : { SIGSEGV, SIGABRT, SIGILL, SIGBUS, SIGFPE }){
        std::signal(sig, onFatalSignal);
    }
    //! Uncaught exception: restore first, then run the handler installed
    //! before (if any) and std::abort() (whose SIGABRT handler then finds
    //! nothing left to restore)
    static std::terminate_handler previous = std::set_terminate([]{
        emergencyRestore();
        if(previous) previous();
        std::abort();
    });
}

Tui::Tui(TuiState& state, ControlQueue& ctl, const ServerModule& server)
    : state_(state), ctl_(ctl), server_(server)
{
    buildItems();
}

Tui::~Tui(){
    stop();
}

bool Tui::start(){
    if(nc_) return true;
    std::setlocale(LC_ALL, "");

    notcurses_options opts{};
    //! SILENT: notcurses must never write diagnostics over the UI.
    //! NO_QUIT_SIGHANDLERS: main() owns SIGINT/SIGTERM/SIGHUP/SIGQUIT and asks
    //! the program to exit through the normal path, which calls stop() and
    //! restores the terminal before the serial port and sockets are closed.
    //! (notcurses' own handlers would re-raise and kill the process.) Crashes
    //! are covered by installCrashHandlers().
    //! SIGWINCH stays with notcurses (delivered to us as NCKEY_RESIZE).
    //! SUPPRESS_BANNERS: no version / performance banners at start and stop.
    //! INHIBIT_SETLOCALE: the locale was set from the environment just above.
    opts.loglevel = NCLOGLEVEL_SILENT;
    opts.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_QUIT_SIGHANDLERS
               | NCOPTION_INHIBIT_SETLOCALE;

    //! Save the terminal state before notcurses changes it (crash path)
    g_haveTermios = tcgetattr(STDIN_FILENO, &g_savedTermios) == 0;
    nc_ = notcurses_core_init(&opts, nullptr);
    if(!nc_) return false;
    g_tuiActive = 1;
    std_ = notcurses_stdplane(nc_);
    //! Base cell: a blank in PHOSPHOR on black, so erased cells are black
    ncplane_set_base(std_, " ", 0, NCCHANNELS_INITIALIZER(0x33, 0xFF, 0x33, 0, 0, 0));

    running_ = true;
    thr_ = std::thread(&Tui::loop, this);
    return true;
}

void Tui::stop(){
    //! The UI thread must be gone before notcurses is stopped
    running_ = false;
    if(thr_.joinable()) thr_.join();
    if(nc_){
        //! notcurses_stop() restores the terminal: the crash path stands down
        g_tuiActive = 0;
        notcurses_stop(nc_);
        nc_ = nullptr;
        std_ = nullptr;
    }
}

void Tui::loop(){
    using namespace std::chrono;
    while(running_.load()){
        ncinput ni{};
        //! notcurses_get() takes an absolute CLOCK_MONOTONIC deadline: wait up
        //! to 50 ms for input, so the screen redraws ~20 times a second when
        //! idle and right after each batch of input
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += 50 * 1000 * 1000;
        if(ts.tv_nsec >= 1000 * 1000 * 1000){ ts.tv_sec += 1; ts.tv_nsec -= 1000 * 1000 * 1000; }
        uint32_t id = notcurses_get(nc_, &ts, &ni);
        int errors = 0;
        //! Handle every event already queued (0 = none left), then draw once
        while(id != 0){
            if(id == static_cast<uint32_t>(-1)){
                ++errors;   //! input error (e.g. tty hung up); avoid spinning
                break;
            }
            //! terminal input ended: ask main() to quit
            if(id == NCKEY_EOF){
                quit_ = true;
                break;
            }
            //! every event but a key release is handled
            if(ni.evtype != NCTYPE_RELEASE) handleKey(ni);
            id = notcurses_get_nblock(nc_, &ni);
        }
        draw();
        notcurses_render(nc_);
        if(errors) std::this_thread::sleep_for(milliseconds(50));
    }
}

void Tui::flash(const std::string& msg, bool error){
    flash_ = msg;
    flashError_ = error;
    flashUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
}

// ============================================================================
//  Input
// ============================================================================

void Tui::LineEdit::handle(const ncinput& ni){
    uint32_t id = ni.id;
    if(id == NCKEY_LEFT){ if(cur > 0) --cur; return; }
    if(id == NCKEY_RIGHT){ if(cur < buf.size()) ++cur; return; }
    if(id == NCKEY_HOME){ cur = 0; return; }
    if(id == NCKEY_END){ cur = buf.size(); return; }
    //! Backspace arrives as NCKEY_BACKSPACE, DEL (0x7f) or BS (0x08)
    if(id == NCKEY_BACKSPACE || id == 0x7f || id == 0x08){
        if(cur > 0){ buf.erase(cur - 1, 1); --cur; }
        return;
    }
    if(id == NCKEY_DEL){
        if(cur < buf.size()) buf.erase(cur, 1);
        return;
    }
    //! Ctrl-U clears the line; any other Ctrl / Alt combination is ignored
    if(ncinput_ctrl_p(&ni) && (id == 'U' || id == 'u')){ clear(); return; }
    if(ncinput_ctrl_p(&ni) || ncinput_alt_p(&ni)) return;
    if(id >= 0x20 && id < 0x7f && buf.size() < 60){    // firmware line buffer is 64 bytes
        buf.insert(cur, 1, static_cast<char>(id));
        ++cur;
    }
}

void Tui::handleKey(const ncinput& ni){
    uint32_t id = ni.id;

    //! Ignore everything during the first moments: some terminals (tmux)
    //! answer startup queries with replies notcurses cannot parse, and those
    //! would otherwise show up as typed text.
    if(std::chrono::steady_clock::now() - t0_ < std::chrono::milliseconds(400)){
        return;
    }

    //! Resize and Ctrl-L: repaint the whole terminal
    bool ctrl = ncinput_ctrl_p(&ni);
    if(id == NCKEY_RESIZE || id == 0x0c || (ctrl && (id == 'L' || id == 'l'))){
        notcurses_refresh(nc_, nullptr, nullptr);
        return;
    }
    //! Quit: F10, Ctrl-Q, Ctrl-C (Ctrl-C normally arrives as SIGINT instead)
    if(id == NCKEY_F10 || id == 0x11 || id == 0x03 ||
       (ctrl && (id == 'Q' || id == 'q' || id == 'C' || id == 'c'))){
        quit_ = true;
        ctl_.push({ControlRequest::Kind::Quit, "", 0});
        return;
    }
    //! F1 and F3 also drop a settings edit or confirmation in progress
    if(id == NCKEY_F01){ screen_ = Screen::Main; editing_ = confirming_ = false; return; }
    if(id == NCKEY_F02){ screen_ = Screen::Settings; return; }
    if(id == NCKEY_F03){ screen_ = Screen::Help; editing_ = confirming_ = false; return; }
    //! Tab: next screen (MAIN -> SETTINGS -> HELP -> MAIN)
    if(id == NCKEY_TAB && !editing_){
        screen_ = screen_ == Screen::Main ? Screen::Settings
                : screen_ == Screen::Settings ? Screen::Help : Screen::Main;
        confirming_ = false;
        return;
    }
    switch(screen_){
    case Screen::Main:     handleMainKey(ni); break;
    case Screen::Settings: handleSettingsKey(ni); break;
    case Screen::Help:     handleHelpKey(ni); break;
    }
}

void Tui::handleMainKey(const ncinput& ni){
    uint32_t id = ni.id;
    if(id == NCKEY_ENTER || id == '\r' || id == '\n'){
        submitCommand(cmd_.buf);
        cmd_.clear();
        return;
    }
    if(id == NCKEY_ESC){
        cmd_.clear();
        chatScroll_ = 0;
        return;
    }
    //! PgUp/PgDn: a page of log lines less one; PgUp stops once the oldest
    //! lines fill the pane
    if(id == NCKEY_PGUP){
        size_t total = state_.chatSize();
        size_t step = static_cast<size_t>(std::max(1, chatLinesShown_ - 1));
        size_t maxScroll = total > static_cast<size_t>(chatPaneRows_) ? total - chatPaneRows_ : 0;
        chatScroll_ = std::min(maxScroll, chatScroll_ + step);
        return;
    }
    if(id == NCKEY_PGDOWN){
        size_t step = static_cast<size_t>(std::max(1, chatLinesShown_ - 1));
        chatScroll_ = chatScroll_ > step ? chatScroll_ - step : 0;
        return;
    }
    //! Up/Down: walk the history; Down past the newest entry gives an empty line
    if(id == NCKEY_UP || id == NCKEY_DOWN){
        if(history_.empty()) return;
        if(id == NCKEY_UP && historyPos_ > 0) --historyPos_;
        if(id == NCKEY_DOWN && historyPos_ < history_.size()) ++historyPos_;
        cmd_.set(historyPos_ < history_.size() ? history_[historyPos_] : "");
        return;
    }
    cmd_.handle(ni);
}

void Tui::submitCommand(const std::string& raw){
    std::string line = raw;
    while(!line.empty() && line.back() == ' ') line.pop_back();
    while(!line.empty() && line.front() == ' ') line.erase(0, 1);
    if(line.empty()) return;

    //! History: no entry twice in a row, the newest 100 kept; Up starts from
    //! the end again
    if(history_.empty() || history_.back() != line) history_.push_back(line);
    if(history_.size() > 100) history_.erase(history_.begin());
    historyPos_ = history_.size();

    //! "help" / "?" is answered here; nothing goes to the base-station
    if(cmdhelp::isHelpRequest(line)){
        printHelp();
        return;
    }

    //! Single-letter commands are case-insensitive for convenience (firmware is not)
    if(line.size() == 1) line[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));
    //! Back to the newest log line, where the reply will show
    chatScroll_ = 0;
    ctl_.push({ControlRequest::Kind::SendCmd, line, 0});
}

void Tui::printHelp(){
    //! Lay the reference out for the log pane's text width in the last draw
    cmdhelp::Writer w(chatTextCols_);
    w.title("COMMAND REFERENCE (ALSO ON THE F3 HELP SCREEN)");
    cmdhelp::appendReference(w);
    std::vector<std::string> lines;
    for(const auto& l : w.lines()) lines.push_back(l.text);
    state_.sysLines(lines);
    //! Show the block from its first line (its lines fit the pane's width, so
    //! one row each); PgDn reads on, Esc jumps to the newest line
    chatScroll_ = lines.size() > static_cast<size_t>(chatPaneRows_)
                ? lines.size() - static_cast<size_t>(chatPaneRows_) : 0;
}

void Tui::draw(){
    unsigned urows = 0, ucols = 0;
    notcurses_stddim_yx(nc_, &urows, &ucols);
    int rows = static_cast<int>(urows), cols = static_cast<int>(ucols);
    ncplane_erase(std_);

    //! The layout needs 80x24: show only a notice below that
    if(rows < 24 || cols < 80){
        theme::ink(std_, theme::AMBER, NCSTYLE_BOLD);
        theme::put(std_, rows / 2, 1, "TERMINAL TOO SMALL - NEED 80x24", cols - 2);
        return;
    }

    //! One consistent copy of the shared state for the whole frame
    TuiSnapshot s = state_.snapshot();
    drawHeader(s, cols);
    switch(screen_){
    case Screen::Main:     drawMain(s, rows, cols); break;
    case Screen::Settings: drawSettings(s, rows, cols); break;
    case Screen::Help:     drawHelp(rows, cols); break;
    }
}
