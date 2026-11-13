//! @file settingsScreen.cpp
//! @brief Tui SETTINGS screen: the settings table and its editor, and the
//!        network server / clients panel.
//!
//! Value items: Enter opens the editor prefilled with the current value, Enter
//! again validates and applies, Esc cancels. Choice items also cycle with
//! Left/Right. Toggles flip on Enter/Space. Actions run on Enter (most send a
//! base-station command); R, D, E and M, which reboot the transmitter or
//! overwrite stored settings, ask for Y/N confirmation first. Info rows show
//! what the base-station last reported (parsed in TuiState), and short notes.
//!
//! Base-station firmware commands (UltraGPS-Arduino, base-station/src/
//! command_control.*; described in CommsModule/commandHelp.cpp): P, S, C, R, D,
//! E, M take no argument; "H <ms>" sets the sampling time (1..min(1000, 80 % of
//! the trigger period) ms), "F <Hz>" the trigger rate (1..50 Hz, both stations;
//! the transmitter stores a new rate and reboots into it) and "B <baud>" the
//! base-station serial baud rate (stored; the reply goes out at the old rate,
//! then the base-station reboots at the new rate and the main loop switches
//! the host to it once the boot time has passed). Any value outside the range
//! changes nothing and just reports the value in force, so "H 0", "F 0" and
//! "B 0" are queries.
//!
//! TRANSMITTER STATION SETTINGS groups R (transmitter reset), D/E (burst delay)
//! and F (trigger rate). They are base-station commands and settings (stored
//! on the base-station) that apply to both stations. The transmitter has no
//! serial command interface; it gets them over the radio: a reset packet (R),
//! the burst delay in every trigger packet (D/E), and the trigger rate (F),
//! which it stores and reboots into when the rate is new to it. M, under BASE STATION, resets them too
//! and re-sends the default rate.
//!
//! @note Everything here except buildItems() (called by the constructor) runs
//!       on the TUI thread. Nothing touches Comms or the serial port: changes
//!       are posted to the main loop as ControlRequests, and their results
//!       show up in later snapshots.

#include "../tui.h"
#include "../theme.h"

#include <notcurses/notcurses.h>
#include <algorithm>
#include <cstdlib>

namespace {

//! Baud rates the host can open (Comms::hostSupportsBaud(), the choices of the
//! -b CLI option); offered for both the host and the base-station rate
const std::vector<std::string> BAUDS = { "9600", "19200", "38400", "57600", "115200" };

//! @brief Format a duration as "hh:mm:ss".
//! @param s Duration in seconds, not negative.
//! @return The duration as "hh:mm:ss" (hours are not wrapped at 24).
std::string hhmmss(long s){
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

//! @brief Age of a wall-clock event.
//! @param t When the event happened; a default-constructed time_point (the
//!          clock's epoch) means it did not happen.
//! @return "hh:mm:ss AGO" (00:00:00 if @p t lies in the future), or "NEVER".
std::string ago(std::chrono::system_clock::time_point t){
    if(t == std::chrono::system_clock::time_point{}) return "NEVER";
    long s = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now() - t).count());
    if(s < 0) s = 0;
    return hhmmss(s) + " AGO";
}

//! @brief Whether the cursor can stop on a settings row: headers and status
//!        (Info) rows are skipped by the cursor.
//! @tparam T Tui::Item::Type. A template because Tui::Item is private: T is
//!           deduced at the call, so this function never has to name it.
//! @param t Row type.
//! @return false for T::Header and T::Info, true for every other type.
template<class T> bool selectable(T t){
    return t != T::Header && t != T::Info;
}

//! @brief Format a base-station value that may not be known yet.
//! @param v    Value; negative = not reported yet (BaseInfo fields start at -1).
//! @param unit Text appended to the value, e.g. " MS".
//! @return "?" for a negative @p v, otherwise the value followed by @p unit.
std::string orUnknown(long v, const std::string& unit){
    return v < 0 ? "?" : std::to_string(v) + unit;
}

//! @brief Parse a decimal integer that must lie in a range.
//! @param s   Text to parse (std::strtol rules: leading blanks and a sign are
//!            accepted); nothing may follow the number.
//! @param lo  Smallest accepted value.
//! @param hi  Largest accepted value.
//! @param out Receives the value on success; left unchanged otherwise.
//! @return true if @p s is a number in [@p lo, @p hi].
bool parseLong(const std::string& s, long lo, long hi, long& out){
    if(s.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if(*end != '\0' || v < lo || v > hi) return false;
    out = v;
    return true;
}

//! @brief Time since a steady_clock instant as "hh:mm:ss".
//! @param since Start time, e.g. when a client connected.
//! @return The time elapsed since @p since, as "hh:mm:ss".
std::string hms(std::chrono::steady_clock::time_point since){
    return hhmmss(static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - since).count()));
}

} // namespace

//! Rows in display order, each { type, label, key, choices, confirm }. The key
//! selects what itemValue(), apply() and runAction() do with the row: every
//! Action row except "clearudp" sends its key as the command line (P, S, C,
//! "H 0", "B 0", M, R, D, E, "F 0"), and Info row keys start with "i_".
void Tui::buildItems(){
    using T = Item::Type;
    items_ = {
        { T::Header, "SERIAL LINK (HOST SIDE)", "", {}, false },
        { T::Value,  "SERIAL PORT", "port", {}, false },
        { T::Choice, "HOST BAUD RATE", "baud", BAUDS, false },
        { T::Value,  "RESPONSE TIMEOUT (MS)", "timeout", {}, false },
        { T::Choice, "LOG LEVEL", "loglevel", { "trace", "debug", "info", "warning", "error", "critical", "off" }, false },
        { T::Header, "NETWORK SERVER", "", {}, false },
        { T::Value,  "MAX CLIENTS (TCP)", "maxclients", {}, false },
        { T::Toggle, "CLIENT LIMIT", "limit", {}, false },
        { T::Action, "FORGET UDP PEERS", "clearudp", {}, false },
        { T::Header, "BASE STATION", "", {}, false },
        { T::Action, "PULSE ............... P", "P", {}, false },
        { T::Action, "SIMULATE PULSE ...... S", "S", {}, false },
        { T::Action, "CONTINUOUS MODE ..... C", "C", {}, false },
        { T::Value,  "SAMPLING MS ........ H", "sampling", {}, false },
        { T::Action, "QUERY SAMPLING ... H 0", "H 0", {}, false },
        { T::Choice, "BASE-STATION BAUD (B)", "basebaud", BAUDS, false },
        { T::Action, "QUERY BAUD ....... B 0", "B 0", {}, false },
        { T::Action, "RESET ALL SETTINGS .. M", "M", {}, true },
        { T::Value,  "RAW COMMAND", "raw", {}, false },
        { T::Info,   "SAMPLING TIME", "i_sampling", {}, false },
        { T::Info,   "SAMPLING COUNTS", "i_counts", {}, false },
        { T::Info,   "STORED BAUD", "i_baud", {}, false },
        { T::Info,   "LAST REPLY", "i_reply", {}, false },
        { T::Header, "TRANSMITTER STATION SETTINGS", "", {}, false },
        { T::Info,   "NOTE", "i_note", {}, false },
        { T::Info,   "APPLIES TO", "i_applies", {}, false },
        { T::Action, "RESET (REBOOTS TX) .. R", "R", {}, true },
        { T::Action, "DELAY SEARCH (~16s) . D", "D", {}, true },
        { T::Action, "DEFAULT DELAY 2000 .. E", "E", {}, true },
        { T::Value,  "TRIGGER HZ 1-50     F", "rate", {}, false },
        { T::Action, "QUERY RATE ....... F 0", "F 0", {}, false },
        { T::Info,   "BURST DELAY", "i_delay", {}, false },
        { T::Info,   "TRIGGER RATE", "i_rate", {}, false },
        { T::Info,   "RATE SENT", "i_ratesent", {}, false },
        { T::Info,   "LAST RESET SENT", "i_reset", {}, false },
        { T::Info,   "LAST DELAY SEARCH", "i_search", {}, false },
        { T::Info,   "SEARCH STEPS SEEN", "i_steps", {}, false },
        { T::Info,   "TX REBOOTS ON", "i_txreboot", {}, false },
    };
}

//! Value column text, also the editor's starting text (beginEdit()). Host
//! values come from the snapshot and the server, base-station values from
//! BaseInfo, "?" while unknown (SAMPLING MS and TRIGGER HZ are left empty
//! instead, so their editor starts empty). Event times read "hh:mm:ss AGO"
//! or NEVER.
std::string Tui::itemValue(const Item& it, const TuiSnapshot& s) const{
    if(it.key == "port")       return s.serialPort;
    if(it.key == "baud")       return std::to_string(s.baud);
    if(it.key == "timeout")    return std::to_string(s.responseTimeout);
    if(it.key == "loglevel")   return s.logLevel;
    if(it.key == "maxclients") return std::to_string(server_.getMaxClients());
    if(it.key == "limit")      return server_.isClientLimitEnabled() ? "[ENABLED]" : "[DISABLED]";
    const BaseInfo& b = s.base;
    if(it.key == "sampling")   return b.samplingMs < 0 ? "" : std::to_string(b.samplingMs);
    if(it.key == "rate")       return b.triggerHz < 0 ? "" : std::to_string(b.triggerHz);
    if(it.key == "basebaud")   return b.baud < 0 ? "?" : std::to_string(b.baud);
    if(it.key == "i_baud")     return b.baud < 0 ? "? (B 0 QUERIES)" : std::to_string(b.baud) +
                                      (b.baud == s.baud ? "  = HOST" : "  != HOST " + std::to_string(s.baud));
    if(it.key == "i_ratesent") return ago(b.txRateTime);
    if(it.key == "i_reset")    return ago(b.txResetTime);
    if(it.key == "i_note")     return "SET ON BASE-STATION";
    if(it.key == "i_applies")  return "BASE + TRANSMITTER";
    if(it.key == "i_txreboot") return "R; F/M NEW RATE";
    if(it.key == "i_delay")    return orUnknown(b.burstDelayUs, " US");
    if(it.key == "i_sampling") return orUnknown(b.samplingMs, " MS");
    if(it.key == "i_counts")   return orUnknown(b.samplingCounts, " x 16 US");
    if(it.key == "i_rate")     return b.triggerHz < 0 ? "?" : std::to_string(b.triggerHz) +
                                      " HZ  TOP " + std::to_string(b.timer1Top);
    if(it.key == "i_search")   return b.searchResult.empty() ? "?" : b.searchResult;
    //! step count, plus " ..." while a long operation (delay search) runs
    if(it.key == "i_steps")    return std::to_string(b.searchSteps) + (s.busy.empty() ? "" : " ...");
    if(it.key == "i_reply")    return b.lastReply.empty() ? "?" : b.lastReply;
    return "";
}

void Tui::beginEdit(const Item& it, const TuiSnapshot& s){
    editing_ = true;
    edit_.set(itemValue(it, s));
}

//! Opens the editor if needed, then steps to the next (dir 1) or previous
//! (dir -1) choice, wrapping around; editor text that is not one of the
//! choices (e.g. "?") starts over at the first choice.
void Tui::cycleChoice(const Item& it, const TuiSnapshot& s, int dir){
    if(it.choices.empty()) return;
    if(!editing_) beginEdit(it, s);
    auto pos = std::find(it.choices.begin(), it.choices.end(), edit_.buf);
    long idx = pos == it.choices.end() ? 0 : pos - it.choices.begin() + dir;
    long n = static_cast<long>(it.choices.size());
    idx = ((idx % n) + n) % n;
    edit_.set(it.choices[idx]);
}

//! Validates the value for the row and posts the matching ControlRequest (a
//! host setting, or a base-station command line), then flashes what happens
//! next. An invalid value posts nothing and returns false: the caller flashes
//! the error and keeps the editor open.
bool Tui::apply(const Item& it, const std::string& value){
    using K = ControlRequest::Kind;
    long v = 0;
    if(it.key == "port"){
        //! absolute device path; the main loop reopens the port at the current baud
        if(value.empty() || value[0] != '/') return false;
        ctl_.push({K::SetPort, value, 0});
        flash("REOPENING " + value, false);
        return true;
    }
    if(it.key == "baud"){
        //! Host side only: the main loop reopens the port at this rate and the
        //! base-station keeps its stored one, so warn when that is known to
        //! differ (fw = the base-station's stored rate, -1 = unknown)
        if(std::find(BAUDS.begin(), BAUDS.end(), value) == BAUDS.end()) return false;
        //! cannot fail: value is one of BAUDS
        parseLong(value, 0, 1000000, v);
        ctl_.push({K::SetBaud, "", v});
        long fw = state_.snapshot().base.baud;
        if(fw > 0 && v != fw){
            flash("HOST ONLY - BASE-STATION STORED " + std::to_string(fw) +
                  " BAUD, USE B TO CHANGE IT", true);
        } else {
            flash("REOPENING HOST AT " + value + " BAUD - MUST MATCH THE BASE-STATION", false);
        }
        return true;
    }
    if(it.key == "basebaud"){
        //! 'B <baud>': the base-station stores it and reboots; the main loop
        //! switches the host to it after the boot time
        if(std::find(BAUDS.begin(), BAUDS.end(), value) == BAUDS.end()) return false;
        ctl_.push({K::SendCmd, "B " + value, 0});
        flash("SENT: B " + value + " - BASE-STATION REBOOTS, HOST SWITCHES AFTER THE BOOT", false);
        return true;
    }
    if(it.key == "timeout"){
        if(!parseLong(value, 50, 60000, v)) return false;
        ctl_.push({K::SetTimeout, "", v});
        flash("RESPONSE TIMEOUT " + value + " MS", false);
        return true;
    }
    if(it.key == "loglevel"){
        if(std::find(it.choices.begin(), it.choices.end(), value) == it.choices.end()) return false;
        ctl_.push({K::SetLogLevel, value, 0});
        flash("LOG LEVEL " + value, false);
        return true;
    }
    if(it.key == "maxclients"){
        if(!parseLong(value, 1, 64, v)) return false;
        ctl_.push({K::SetMaxClients, "", v});
        flash("MAX CLIENTS " + value, false);
        return true;
    }
    if(it.key == "sampling"){
        //! upper limit from the last reported Timer1 TOP (1000 ms while unknown)
        long maxMs = baseMaxSamplingMs(state_.snapshot().base.timer1Top);
        if(!parseLong(value, BASE_SAMPLING_MIN_MS, maxMs, v)) return false;
        ctl_.push({K::SendCmd, "H " + std::to_string(v), 0});
        flash("SENT: H " + std::to_string(v), false);
        return true;
    }
    if(it.key == "rate"){
        if(!parseLong(value, BASE_RATE_MIN_HZ, BASE_RATE_MAX_HZ, v)) return false;
        ctl_.push({K::SendCmd, "F " + std::to_string(v), 0});
        flash("SENT: F " + std::to_string(v) + " - A NEW RATE RESETS THE TRANSMITTER", false);
        return true;
    }
    if(it.key == "raw"){
        //! Same path as the MAIN command line: submitCommand() adds the line to
        //! the command history, prints the reference for help / ?, and posts
        //! anything else for the base-station
        if(value.empty()) return false;
        submitCommand(value);
        if(cmdhelp::isHelpRequest(value)) flash("COMMAND REFERENCE PRINTED TO THE F1 MAIN LOG, ALSO ON F3 HELP", false);
        else flash("SENT: " + value, false);
        return true;
    }
    return false;
}

//! Toggle and Action rows: flips the TCP client limit, forgets the UDP peers,
//! or sends the row's key as a base-station command line; then flashes what
//! happens next.
void Tui::runAction(const Item& it){
    using K = ControlRequest::Kind;
    if(it.key == "limit"){
        bool on = !server_.isClientLimitEnabled();
        ctl_.push({K::SetLimitEnabled, "", on ? 1 : 0});
        flash(on ? "CLIENT LIMIT ENABLED" : "CLIENT LIMIT DISABLED", false);
    } else if(it.key == "clearudp"){
        ctl_.push({K::ClearUdpPeers, "", 0});
        flash("UDP PEERS CLEARED", false);
    } else {
        ctl_.push({K::SendCmd, it.key, 0});
        //! M reboots the base-station only when its stored rate (the host's,
        //! on a working link) is not the default 38400; the host follows it
        if(it.key == "M" && state_.snapshot().baud != BASE_DEFAULT_BAUD){
            flash("SENT M - BASE-STATION REBOOTS AT 38400 BAUD, HOST FOLLOWS AFTER THE BOOT", false);
        } else if(it.key == "R"){
            flash("SENT R - RESET PACKET, TRANSMITTER REBOOTS", false);
        } else if(it.key == "E"){
            flash("SENT E - TRIGGER PACKETS NOW CARRY 2000 US", false);
        } else if(it.key == "D"){
            flash("SENT D - SWEEP RUNS ~16 S, LINK BUSY", false);
        } else {
            flash("SENT " + it.key, false);
        }
    }
}

//! Key handling, in order: an open confirmation takes any key (Y runs the
//! action, anything else cancels it); an open editor takes Enter (apply), Esc
//! (cancel), Left/Right on a choice (cycle) and editing keys; otherwise the
//! keys move the selection, Esc goes back to MAIN, Left/Right cycle a choice
//! and Enter/Space act on the selected row. Screen switching (F1-F3, Tab
//! outside the editor), quitting and redraws are handled before this, by
//! handleKey().
void Tui::handleSettingsKey(const ncinput& ni){
    uint32_t id = ni.id;
    TuiSnapshot s = state_.snapshot();
    const Item& it = items_[sel_];
    bool enter = id == NCKEY_ENTER || id == '\r' || id == '\n';

    if(confirming_){
        confirming_ = false;
        if(id == 'y' || id == 'Y') runAction(it);
        else flash("CANCELLED", false);
        return;
    }

    if(editing_){
        if(enter){
            //! an invalid value keeps the editor open and flashes the error (with
            //! the valid range for SAMPLING MS and TRIGGER HZ)
            if(apply(it, edit_.buf)) editing_ = false;
            else if(it.key == "sampling") flash("INVALID VALUE - SAMPLING TIME IS 1.." +
                std::to_string(baseMaxSamplingMs(s.base.timer1Top)) + " MS", true);
            else if(it.key == "rate") flash("INVALID VALUE - TRIGGER RATE IS 1..50 HZ", true);
            else flash("INVALID VALUE", true);
            return;
        }
        if(id == NCKEY_ESC){ editing_ = false; return; }
        if(it.type == Item::Type::Choice && (id == NCKEY_LEFT || id == NCKEY_RIGHT)){
            cycleChoice(it, s, id == NCKEY_RIGHT ? 1 : -1);
            return;
        }
        edit_.handle(ni);
        return;
    }

    //! move(dir): select the next selectable row in direction dir (1 down,
    //! -1 up), wrapping around at the ends
    auto move = [&](int dir){
        int n = static_cast<int>(items_.size());
        int i = sel_;
        do { i = (i + dir + n) % n; }
        while(!selectable(items_[i].type));
        sel_ = i;
    };
    //! jump(from, dir): select row from, or the next selectable row after it in direction dir
    auto jump = [&](int from, int dir){
        sel_ = from;
        if(!selectable(items_[sel_].type)) move(dir);
    };
    if(id == NCKEY_UP || id == 'k'){ move(-1); return; }
    if(id == NCKEY_DOWN || id == 'j'){ move(1); return; }
    if(id == NCKEY_HOME || id == 'g'){ jump(0, 1); return; }
    if(id == NCKEY_END || id == 'G'){ jump(static_cast<int>(items_.size()) - 1, -1); return; }
    //! PgUp/PgDn: 8 selectable rows at a time (wrapping like Up/Down)
    if(id == NCKEY_PGDOWN || id == NCKEY_PGUP){
        int d = id == NCKEY_PGDOWN ? 1 : -1;
        for(int k = 0; k < 8; ++k) move(d);
        return;
    }
    if(id == NCKEY_ESC){ screen_ = Screen::Main; return; }
    if(it.type == Item::Type::Choice && (id == NCKEY_LEFT || id == NCKEY_RIGHT)){
        cycleChoice(it, s, id == NCKEY_RIGHT ? 1 : -1);
        return;
    }
    //! Enter edits a value or choice and runs an action (after the Y/N prompt
    //! if it has one); Space only flips toggles
    if(enter || id == ' '){
        switch(it.type){
        case Item::Type::Value:
        case Item::Type::Choice:
            if(enter) beginEdit(it, s);
            break;
        case Item::Type::Toggle:
            runAction(it);
            break;
        case Item::Type::Action:
            if(!enter) break;
            if(it.confirm){
                confirming_ = true;
            } else {
                runAction(it);
            }
            break;
        case Item::Type::Header:
        case Item::Type::Info:
            break;
        }
    }
}

//! Layout below the header: the settings list framed on the left (55 % of the
//! width, at least 44 columns) and the network server panel on the right,
//! then the confirmation / editor / message line on row rows-2 and the key
//! help on the last row.
void Tui::drawSettings(const TuiSnapshot& s, int rows, int cols){
    using namespace theme;
    const int y = 5, h = rows - 2 - y;
    const int lw = std::max(44, cols * 55 / 100);
    frame(std_, y, 0, h, lw, CYAN, "SYSTEM CONFIGURATION");
    drawServerPanel(s, y, lw, h, cols - lw);

    //! Scroll the list so the selection stays visible together with the rows
    //! that cannot be selected around it (section headers above, status rows
    //! below, which are otherwise unreachable), as far as they fit.
    const int ih = std::max(1, h - 2);
    const int n = static_cast<int>(items_.size());
    int ctxTop = sel_, ctxEnd = sel_;
    while(ctxTop > 0 && !selectable(items_[ctxTop - 1].type)) --ctxTop;
    while(ctxEnd + 1 < n && !selectable(items_[ctxEnd + 1].type)) ++ctxEnd;
    int first = std::clamp(settingsScroll_, 0, std::max(0, n - ih));
    if(ctxEnd >= first + ih) first = ctxEnd - ih + 1;       //! trailing status rows
    if(ctxTop < first) first = std::max(ctxTop, sel_ - ih + 1);  //! header above
    settingsScroll_ = first;
    //! scroll hints: ▲ / ▼ on the frame while rows are hidden above / below
    if(first > 0){ ink(std_, CYAN_DIM); put(std_, y, lw - 5, "▲"); }
    if(first + ih < n){ ink(std_, CYAN_DIM); put(std_, y + h - 1, lw - 5, "▼"); }
    //! value column: dot leaders run up to it, values start 2 columns right
    //! of it (Info values 6 columns left of it)
    const int valueX = 2 + 27;
    for(int row = 0; row < ih && first + row < n; ++row){
        int idx = first + row;
        const Item& it = items_[idx];
        int ry = y + 1 + row;
        if(it.type == Item::Type::Header){
            ink(std_, CYAN_DIM, NCSTYLE_BOLD);
            put(std_, ry, 2, "── " + it.label + " ──", lw - 4);
            continue;
        }
        if(it.type == Item::Type::Info){
            //! status row: dim label, cyan value (STORED BAUD in red when it is
            //! known to differ from the host rate)
            ink(std_, PHOS_DIM);
            put(std_, ry, 5, it.label, lw - 6);
            ink(std_, it.key == "i_baud" && s.base.baud > 0 && s.base.baud != s.baud ? ERROR : CYAN,
                NCSTYLE_BOLD);
            put(std_, ry, valueX - 6, itemValue(it, s), lw - (valueX - 6) - 2);
            continue;
        }
        bool selected = idx == sel_;
        //! label at column 5, dot leaders up to the value column
        const int labelX = 5;
        int dotsX = labelX + theme::cols(it.label) + 1;
        if(selected){
            //! selection: ▶ marker and the label in inverse orange
            ink(std_, ACCENT, NCSTYLE_BOLD);
            put(std_, ry, 2, "▶");
            ncplane_set_fg_rgb(std_, BG);
            ncplane_set_bg_rgb(std_, ACCENT);
            ncplane_set_styles(std_, NCSTYLE_BOLD);
            put(std_, ry, labelX - 1, " " + it.label + " ", lw - labelX - 1);
        } else {
            ink(std_, PHOSPHOR);
            put(std_, ry, labelX, it.label, lw - labelX - 1);
        }
        if(it.type != Item::Type::Action){
            //! value, or the editor's text while this row is being edited
            std::string dots;
            for(int c = dotsX; c < valueX + 1; ++c) dots += ".";
            ink(std_, selected ? ACCENT : PHOS_DIM);
            put(std_, ry, dotsX, dots);
            std::string v = (selected && editing_) ? edit_.buf : itemValue(it, s);
            uint32_t vc = AMBER;
            if(it.key == "limit") vc = server_.isClientLimitEnabled() ? PHOSPHOR : ERROR;
            //! host and base-station baud rates known to differ: the link is
            //! broken, or the host has not yet followed a B / M reboot
            if((it.key == "baud" || it.key == "basebaud") && s.base.baud > 0 &&
               s.base.baud != s.baud) vc = ERROR;
            ink(std_, vc, NCSTYLE_BOLD);
            put(std_, ry, valueX + 2, v, lw - valueX - 4);
        } else if(selected){
            ink(std_, AMBER, NCSTYLE_BOLD);
            put(std_, ry, lw - 10, "[ENTER]");
        }
    }

    //! bottom: confirmation prompt, else editor, else flash message (3 s),
    //! else a hint
    bool flashing = std::chrono::steady_clock::now() < flashUntil_;
    if(confirming_){
        ink(std_, ERROR, NCSTYLE_BOLD);
        put(std_, rows - 2, 1, "CONFIRM " + items_[sel_].label + " ?  Y/N", cols - 2);
    } else if(editing_){
        drawPrompt(rows - 2, cols, "VALUE> ", edit_);
    } else if(flashing){
        ink(std_, flashError_ ? ERROR : PHOSPHOR, NCSTYLE_BOLD);
        put(std_, rows - 2, 1, flash_, cols - 2);
    } else {
        ink(std_, PHOS_DIM);
        put(std_, rows - 2, 1, "SELECT A SETTING.", cols - 2);
    }
    //! an error while editing replaces the key help, so the editor stays visible
    if(editing_ && flashing && flashError_){
        ink(std_, ERROR, NCSTYLE_BOLD);
        put(std_, rows - 1, 1, flash_, cols - 2);
        return;
    }
    ink(std_, CYAN_DIM);
    put(std_, rows - 1, 1,
        editing_ ? "ENTER APPLY  ESC CANCEL  LEFT/RIGHT CYCLE CHOICES"
                 : "UP/DN PGUP/PGDN HOME/END  ENTER EDIT/SEND  ESC MAIN  F3 HELP  F10 QUIT",
        cols - 2);
}

//! Network server panel: IP and network device, TCP and UDP ports with their
//! state, the TCP client count against the limit (OVER when more are connected
//! than it allows) and the UDP peer count, then one row per client (protocol,
//! address, time since it connected or, for UDP, since its first datagram).
//! The last inner row shows the host baud rate and the base-station's stored
//! rate ("?" until reported).
void Tui::drawServerPanel(const TuiSnapshot& s, int y, int x, int h, int w){
    using namespace theme;
    frame(std_, y, x, h, w, CYAN, "NETWORK SERVER");
    const int ix = x + 2, iw = w - 4;
    int ry = y + 1;
    const int last = y + h - 2;
    //! line(): print one row and move down; rows past the last inner row are dropped
    auto line = [&](uint32_t color, const std::string& text, uint16_t st = NCSTYLE_NONE){
        if(ry > last) return;
        ink(std_, color, st);
        put(std_, ry++, ix, text, iw);
    };

    bool up = server_.isRunning();
    line(PHOSPHOR, "IP   " + server_.getIP() + " (" + server_.getNetDevice() + ")", NCSTYLE_BOLD);
    line(up ? PHOSPHOR : ERROR, "TCP  " + std::to_string(server_.getTcpPort()) + (up ? "  [LISTENING]" : "  [DOWN]"));
    line(up ? PHOSPHOR : ERROR, "UDP  " + std::to_string(server_.getUdpPort()) + (up ? "  [BOUND]" : "  [DOWN]"));

    //! more TCP clients than the limit: it was lowered or enabled while they
    //! were connected (existing sessions are kept)
    size_t tcpN = server_.tcpClientCount();
    bool lim = server_.isClientLimitEnabled();
    std::string cap = lim ? std::to_string(server_.getMaxClients()) : "UNLIMITED";
    bool over = lim && tcpN > server_.getMaxClients();
    line(over ? ERROR : AMBER, "CLIENTS " + std::to_string(tcpN) + " / " + cap +
         (over ? "  OVER" : "") + "   UDP PEERS " + std::to_string(server_.udpClientCount()), NCSTYLE_BOLD);

    if(ry <= last){
        ink(std_, CYAN_DIM);
        std::string rule;
        for(int i = 0; i < iw; ++i) rule += "─";
        put(std_, ry++, ix, rule);
    }

    //! clients: TCP (green) first, then UDP peers (cyan); addresses padded to
    //! 22 columns so the times line up
    auto clients = server_.getClients();
    if(clients.empty()){
        line(PHOS_DIM, "NO CLIENTS CONNECTED");
    }
    for(auto& c : clients){
        std::string addr = c.ip + ":" + std::to_string(c.port);
        while(theme::cols(addr) < 22) addr += " ";
        line(c.proto == "TCP" ? PHOSPHOR : CYAN, c.proto + "  " + addr + hms(c.since));
    }

    //! footnote at the bottom of the panel
    ink(std_, PHOS_DIM);
    const long fw = s.base.baud;
    put(std_, last, ix, "HOST " + std::to_string(s.baud) + " BAUD  BASE " +
        (fw < 0 ? std::string("?") : std::to_string(fw)), iw);
}
