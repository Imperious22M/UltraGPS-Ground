//! @file tui.h
//! @brief Tui: the ground station's terminal UI (notcurses-core, --tui).
//!
//! Three screens:
//! - MAIN: big block-digit tick readout per receiver with its jitter, one
//!   60 s graph per receiver, serial "chat log" (every line sent to /
//!   received from the base-station, plus log lines and TUI messages) and a
//!   command line.
//! - SETTINGS: host + base-station settings (select, type value, Enter) and
//!   the network server panel (IP, ports, connected clients).
//! - HELP: the command reference (CommsModule/commandHelp) and the keys.
//!
//! The TUI owns one thread that reads input and renders. It never touches
//! Comms or the serial port: it reads TuiState / ServerModule (both
//! thread-safe) and posts ControlRequests that the main loop executes.
//!
//! Tui is implemented in tui.cpp, screens/mainScreen.cpp,
//! screens/settingsScreen.cpp and screens/helpScreen.cpp, which are only
//! compiled when the TUI is enabled (ULTRAGPS_TUI).

#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "tuiState.h"
#include "../ServerModule/serverModule.h"
#include "../CommsModule/commandHelp.h"

struct notcurses;
struct ncplane;
struct ncinput;

//! @brief The ground station's terminal UI: one thread that reads the keys and
//!        draws the MAIN, SETTINGS and HELP screens.
//!
//! Used by main(): construct, installCrashHandlers(), start(), poll
//! quitRequested(), stop() (the destructor stops it too).
//! @note After start(), the private state is used by the UI thread only, so it
//!       needs no lock. While it runs, other threads call only
//!       quitRequested() (an atomic read) and stop() (which joins the UI
//!       thread before it touches anything else).
class Tui {
public:
    //! @brief Store the shared objects and build the settings table.
    //!
    //! Does not touch the terminal; start() does.
    //! @param state  Shared state the screens are drawn from (the command
    //!               reference is also printed into its chat log).
    //! @param ctl    Queue for the requests the main loop executes.
    //! @param server Network server, read for the header and the SETTINGS panel.
    Tui(TuiState& state, ControlQueue& ctl, const ServerModule& server);
    //! @brief Calls stop(): joins the UI thread and restores the terminal.
    ~Tui();

    //! @brief Not copyable: a Tui owns the UI thread and the terminal
    Tui(const Tui&) = delete;
    Tui& operator=(const Tui&) = delete;

    //! @brief Initialise notcurses (takes over the terminal) and spawn the UI thread.
    //!
    //! First sets the locale from the environment and saves the terminal
    //! settings for the crash handlers.
    //! @return true once running (also if it already was); false if the
    //!         terminal cannot be driven (no TTY, unknown TERM).
    bool start();

    //! @brief Restore the terminal on SIGSEGV/SIGABRT/SIGILL/SIGBUS/SIGFPE and on
    //!        std::terminate. Call once, before start().
    //!
    //! Each handler restores the terminal if the TUI still owns it, then
    //! carries on as without it: the signal gets its default action, and
    //! std::terminate calls the previously installed handler, then std::abort().
    static void installCrashHandlers();

    //! @brief Join the UI thread and restore the terminal. Idempotent.
    //! @note Call it from the thread that called start(), never from the UI
    //!       thread (it joins that thread).
    void stop();

    //! @brief True once the user asked to quit (F10 / Ctrl-Q, or Ctrl-C when it
    //!        arrives as a key rather than SIGINT) or the terminal input ended.
    //! @return Whether the program should exit.
    //! @note Thread-safe (atomic); main() polls it.
    bool quitRequested() const { return quit_.load(); }

private:
    //! @brief Screen shown under the header: MAIN (F1), SETTINGS (F2) or HELP
    //!        (F3); Tab steps through them in that order.
    enum class Screen { Main, Settings, Help };

    //! @brief Settings table entry: one row of the SETTINGS screen.
    struct Item {
        //! @brief Kind of row.
        //!
        //! Header: section title. Value: Enter opens the editor prefilled with
        //! the current value, Enter again applies it. Choice: like Value, and
        //! Left/Right step through the choices. Toggle: Enter or Space flips
        //! it. Action: Enter runs it, after a Y/N confirmation if confirm is
        //! set. Info: read-only row (base-station status or a note), skipped
        //! by the cursor, like Header.
        enum class Type { Header, Value, Choice, Toggle, Action, Info };
        Type type;          //!< kind of row
        std::string label;  //!< text shown in the row
        std::string key;                    //!< identifier used by itemValue(), apply() and
                                            //!< runAction(); a base-station Action sends it as
                                            //!< the command line
        std::vector<std::string> choices;   //!< for Choice
        bool confirm = false;               //!< Action asks "CONFIRM <label> ? Y/N" first
    };

    //! @brief Minimal single-line editor (printable ASCII only: command lines
    //!        end up on the serial link).
    struct LineEdit {
        std::string buf;    //!< the text
        size_t cur = 0;     //!< cursor: byte index into buf, 0..buf.size()

        //! @brief Replace the text and put the cursor at its end.
        //! @param s New text.
        void set(const std::string& s){ buf = s; cur = s.size(); }
        //! @brief Empty the text.
        void clear(){ buf.clear(); cur = 0; }
        //! @brief Apply one key: Left/Right/Home/End move the cursor,
        //!        Backspace/Delete erase, Ctrl-U clears, printable ASCII is
        //!        inserted at the cursor; other keys are ignored.
        //!
        //! The text stops growing at 60 characters, so a command fits the
        //! firmware's 64-byte line buffer.
        //! @param ni The key event.
        void handle(const ncinput& ni);
    };

    //! @name Core, input and command line (tui.cpp)
    //! @{

    //! @brief UI thread body: wait up to 50 ms for input, handle every queued
    //!        key, then draw and render; runs until stop() clears running_.
    //!
    //! Key releases are ignored. The end of terminal input sets quit_; after
    //! an input error the loop pauses 50 ms instead of spinning.
    void loop();
    //! @brief Draw one frame on the standard plane (loop() renders it): the
    //!        header and the active screen from a fresh TuiSnapshot, or only
    //!        a notice when the terminal is smaller than 80x24.
    void draw();
    //! @brief Keys for every screen, then the active screen's handler.
    //!
    //! All input is ignored for 400 ms after start-up (terminal replies that
    //! notcurses cannot parse). Resize and Ctrl-L repaint the terminal; F10,
    //! Ctrl-Q and Ctrl-C quit (quit_ plus a Quit request); F1/F2/F3 pick a
    //! screen and Tab the next one (not while a value is being edited).
    //! @param ni The key event.
    void handleKey(const ncinput& ni);
    //! @brief MAIN screen keys: Enter submits the command line, Esc clears it
    //!        and jumps to the newest log line, PgUp/PgDn scroll the log,
    //!        Up/Down recall the history; other keys edit the line.
    //! @param ni The key event.
    void handleMainKey(const ncinput& ni);
    //! @brief Show a message at the bottom of the SETTINGS screen for 3 s.
    //! @param msg   Message text.
    //! @param error true: drawn in red (ERROR); false: in green.
    void flash(const std::string& msg, bool error);
    //! @brief Command line of the MAIN screen and the settings RAW COMMAND item:
    //!        "help" / "?" prints the reference into the log, anything else is
    //!        posted to the main loop for the base-station.
    //!
    //! Leading and trailing spaces are dropped; a blank line does nothing.
    //! The line goes into the history (no entry twice in a row, at most 100).
    //! A command for the base-station is upper-cased when it is a single
    //! letter, the log jumps to its newest line, and the line is posted as
    //! ControlRequest::Kind::SendCmd.
    //! @param line The line as typed.
    void submitCommand(const std::string& line);
    //! @brief Print the command reference into the chat log as one block of TUI
    //!        lines, wrapped to the log pane, and scroll the pane to its first line.
    void printHelp();

    //! @}

    //! @name MAIN screen and header (screens/mainScreen.cpp)
    //! @{

    //! @brief Header, rows 0-4 of every screen: banner, version and clock;
    //!        serial line (port @ baud, LINK ONLINE / BOOTING / OFFLINE);
    //!        network line (IP, ports, clients); gradient rule; screen tabs
    //!        with the mode, RX/TX counters and a blinking busy / booting
    //!        banner.
    //! @param s    Snapshot to draw.
    //! @param cols Terminal width.
    void drawHeader(const TuiSnapshot& s, int cols);
    //! @brief MAIN screen below the header: tick box (rows 5-11), receiver
    //!        graphs (left) and chat log (right), then the command line and
    //!        the key hints on the last two rows.
    //! @param s         Snapshot to draw.
    //! @param rows,cols Terminal size.
    void drawMain(const TuiSnapshot& s, int rows, int cols);
    //! @brief Framed tick box, one cell per receiver: label "R<n>", the last
    //!        reading in block digits (dashes before any data, NO ECHO for
    //!        0, plain text when too wide) and its jitter on the row below.
    //!        Dimmed when no tick line arrived for 5 s.
    //! @param s   Snapshot to draw.
    //! @param y,x Top-left corner.
    //! @param h,w Size, border included (drawMain() uses 7 rows: border,
    //!            label, 3 digit rows, jitter, border).
    void drawTicks(const TuiSnapshot& s, int y, int x, int h, int w);
    //! @brief The six receiver graphs as a grid of 1x6, 2x3, 3x2 or 6x1
    //!        (columns x rows), picked per frame to leave each plot least
    //!        cramped; "GRAPH AREA TOO SMALL" when none fits.
    //! @param s   Snapshot to draw.
    //! @param y,x Top-left corner of the area.
    //! @param h,w Size of the area.
    void drawGraph(const TuiSnapshot& s, int y, int x, int h, int w);
    //! @brief One receiver's framed graph of the last 60 s (per-second
    //!        maximum), scaled to its own non-zero range: current value in
    //!        the title, range in the bottom border, a red dot for a second
    //!        with no echo.
    //! @param s   Snapshot to draw.
    //! @param i   Receiver index, from 0.
    //! @param y,x Top-left corner.
    //! @param h,w Size, border included.
    void drawRecvGraph(const TuiSnapshot& s, size_t i, int y, int x, int h, int w);
    //! @brief The SERIAL LINK pane: the chat log, newest line at the bottom,
    //!        scrolled back by chatScroll_. Each row shows the time, a mark
    //!        (> sent, < received, * TUI, log level) and the text; TUI lines
    //!        wrap, the others are clipped.
    //!
    //! Records chatPaneRows_, chatLinesShown_ and chatTextCols_ for the
    //! scroll keys and printHelp().
    //! @param y,x Top-left corner.
    //! @param h,w Size, border included.
    void drawChat(int y, int x, int h, int w);
    //! @brief Prompt row: label, then the editor's text scrolled sideways to
    //!        keep the cursor visible, with a blinking block cursor.
    //! @param y     Row.
    //! @param cols  Terminal width.
    //! @param label Prompt, e.g. "CMD> " or "VALUE> ".
    //! @param ed    Editor to show.
    void drawPrompt(int y, int cols, const std::string& label, const LineEdit& ed);

    //! @}

    //! @name SETTINGS screen (screens/settingsScreen.cpp)
    //! @{

    //! @brief Fill items_ with the settings table: SERIAL LINK (HOST SIDE),
    //!        NETWORK SERVER, BASE STATION and TRANSMITTER STATION SETTINGS.
    void buildItems();
    //! @brief SETTINGS screen below the header: the settings table (left,
    //!        scrolled so the selection and the header and status rows around
    //!        it stay visible), the network server panel (right), then the
    //!        confirmation, editor, flash message or hint, and the key hints.
    //! @param s         Snapshot to draw.
    //! @param rows,cols Terminal size.
    void drawSettings(const TuiSnapshot& s, int rows, int cols);
    //! @brief NETWORK SERVER panel: IP and device, TCP/UDP ports and state,
    //!        clients against the cap, one row per connected client, and a
    //!        HOST / BASE baud footnote.
    //! @param s   Snapshot to draw (for the baud rates).
    //! @param y,x Top-left corner.
    //! @param h,w Size, border included.
    void drawServerPanel(const TuiSnapshot& s, int y, int x, int h, int w);
    //! @brief Value text of a settings row.
    //! @param it Row.
    //! @param s  Snapshot with the link and base-station state (some rows
    //!           read the server or show a fixed note instead).
    //! @return The text; "?" or empty when unknown, empty for rows without a value.
    std::string itemValue(const Item& it, const TuiSnapshot& s) const;
    //! @brief Open the value editor prefilled with itemValue().
    //! @param it Row to edit.
    //! @param s  Snapshot for its current value.
    void beginEdit(const Item& it, const TuiSnapshot& s);
    //! @brief Step a Choice row's editor value to the next or previous choice,
    //!        wrapping around; opens the editor first if needed. A value that
    //!        is not one of the choices goes to the first one.
    //! @param it  Row (Choice).
    //! @param s   Snapshot for its current value.
    //! @param dir 1 for the next choice, -1 for the previous one.
    void cycleChoice(const Item& it, const TuiSnapshot& s, int dir);
    //! @brief Validate an edited value and post the matching ControlRequest
    //!        (RAW COMMAND goes through submitCommand()), then flash what
    //!        happens.
    //! @param it    Row being edited.
    //! @param value Text from the editor.
    //! @return true if accepted (the editor closes), false if the value is invalid.
    bool apply(const Item& it, const std::string& value);
    //! @brief Run a Toggle or Action row: flip the TCP client limit, forget the
    //!        UDP peers, or send the row's key as a base-station command; then
    //!        flash what happens.
    //! @param it Row to run.
    void runAction(const Item& it);
    //! @brief SETTINGS keys. A pending confirmation takes the next key (Y runs
    //!        the action, anything else cancels). While editing: Enter
    //!        applies, Esc cancels, Left/Right cycle a choice, other keys
    //!        edit. Otherwise Up/Down (j/k), PgUp/PgDn (8 rows) and Home/End
    //!        (g/G) move the selection, Left/Right cycle a choice, Enter (or
    //!        Space for a toggle) activates the row and Esc returns to MAIN.
    //! @param ni The key event.
    void handleSettingsKey(const ncinput& ni);

    //! @}

    //! @name HELP screen (screens/helpScreen.cpp)
    //! @{

    //! @brief HELP screen below the header: the scrollable text of helpLines()
    //!        (at most 100 columns wide), the position and a tip, and the key
    //!        hints.
    //! @param rows,cols Terminal size.
    void drawHelp(int rows, int cols);
    //! @brief HELP keys: Up/Down (j/k) scroll a line, PgUp/PgDn (Space too) a
    //!        page, Home/End (g/G) go to the top and the bottom, Esc returns to
    //!        MAIN.
    //! @param ni The key event.
    void handleHelpKey(const ncinput& ni);
    //! @brief Lines of the HELP screen for a text width (cached in helpLines_):
    //!        the command reference, the TUI keys and the MAIN screen readouts.
    //! @param width Text width in columns; the text is laid out again only
    //!              when it changes.
    //! @return helpLines_.
    const std::vector<cmdhelp::Line>& helpLines(int width);

    //! @}

    TuiState& state_;               //!< shared display state and chat log
    ControlQueue& ctl_;             //!< requests for the main loop
    const ServerModule& server_;    //!< network server (thread-safe getters)

    notcurses* nc_ = nullptr;       //!< notcurses context; null while stopped
    ncplane* std_ = nullptr;        //!< standard plane (the whole terminal)
    std::thread thr_;               //!< UI thread, runs loop()
    std::atomic<bool> running_{false};  //!< loop() runs while true; stop() clears it
    std::atomic<bool> quit_{false};     //!< returned by quitRequested()

    Screen screen_ = Screen::Main;  //!< screen shown under the header

    //! @name MAIN screen state
    //! @{

    LineEdit cmd_;                      //!< command line
    std::vector<std::string> history_;  //!< submitted lines, oldest first (at most 100)
    size_t historyPos_ = 0;             //!< history_ entry shown; history_.size() = new line
    size_t chatScroll_ = 0;     //!< lines scrolled back from the newest
    int chatPaneRows_ = 10;     //!< rows of the chat pane in the last draw
    int chatLinesShown_ = 10;   //!< chat lines visible in the last draw (PgUp/PgDn move one less)
    int chatTextCols_ = 60;     //!< text columns of the chat pane in the last draw

    //! @}

    //! @name SETTINGS screen state
    //! @{

    std::vector<Item> items_;   //!< settings table (buildItems())
    int sel_ = 1;               //!< selected row in items_ (starts on 1, the first selectable one)
    int settingsScroll_ = 0;    //!< first visible settings row
    bool editing_ = false;      //!< the value editor edit_ is open
    bool confirming_ = false;   //!< waiting for Y before running the selected action
    LineEdit edit_;             //!< value editor
    std::string flash_;         //!< message set by flash()
    bool flashError_ = false;   //!< flash_ is an error (red)
    std::chrono::steady_clock::time_point flashUntil_{};    //!< flash_ is shown until then

    //! @}

    //! @name HELP screen state
    //! @{

    int helpScroll_ = 0;        //!< first visible help line
    int helpPage_ = 10;         //!< visible help lines in the last draw (PgUp/PgDn move one less)
    int helpWidth_ = 0;         //!< width helpLines_ was laid out for
    std::vector<cmdhelp::Line> helpLines_;  //!< HELP text cached by helpLines()

    //! @}

    //! Creation time: drives the logo shimmer, the blinking cursor and banner,
    //! and the 400 ms startup input filter
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
};
