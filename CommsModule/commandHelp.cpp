//! @file commandHelp.cpp
//! @brief Command reference: the base-station command table and the line
//!        layout (word wrap, wide and stacked entries).
//!
//! The API is documented in commandHelp.h.

#include "commandHelp.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace cmdhelp {

namespace {

constexpr int KEY_COLS = 13;     //!< wide layout: columns of the key column
constexpr int LABEL_COLS = 9;    //!< wide layout: columns of a detail line's label
constexpr int WIDE_MIN = 60;     //!< narrower than this, entries use the stacked layout

//! One base-station command (firmware base-station/src/command_control.*)
struct Entry {
    const char* syntax;     //!< what to type
    bool transmitter;       //!< also acts on the transmitter station
    const char* summary;    //!< what it does
    const char* reply;      //!< reply format
    const char* example;    //!< command -> reply
};

//! The base-station commands, in the order the reference lists them
const Entry BASE_STATION[] = {
    { "P", false,
      "Pulse: fire one pulse and measure the echo at each receiver.",
      "N: t0, t1, t2, t3, t4, t5 (Timer1 counts of 16 us per receiver, 0 = no echo)",
      "P -> N: 4404, 3226, 4063, 4041, 3133, 4109" },
    { "S", false,
      "Simulated pulse: like P, but with simulated tick counts.",
      "N: t0, t1, t2, t3, t4, t5",
      "S -> N: 4400, 3227, 4064, 4040, 3134, 4108" },
    { "C", false,
      "Continuous mode: measure every trigger period (about 10 lines/s at 10 Hz) "
      "until another command arrives.",
      "a stream of C: t0, t1, t2, t3, t4, t5 lines; the ground station relays them "
      "on the UDP port (no TCP reply)",
      "C -> C: 4400, 3227, 4064, 4040, 3134, 4108 ..." },
    { "R", true,
      "Reset the transmitter station (reset packet); it reboots.",
      "N: R",
      "R -> N: R" },
    { "D", true,
      "Burst-delay search: sweep 0 to 500 us in 10 us steps, 3 pulses each (about "
      "16 s at 10 Hz), then store and use the delay with the most valid readings "
      "(the lowest mean on a tie).",
      "D: <us>, <mean ticks>, <valid>/<total> per step, then the result in the same "
      "format (TCP clients get the result only)",
      "D -> D: 120, 3987, 18/18" },
    { "E", true,
      "Store the default burst delay, 2000 us (sent in every trigger packet).",
      "E: <us>",
      "E -> E: 2000" },
    { "H <ms>", false,
      "Sampling time, 1 ms up to 80 % of the trigger period (80 ms at 10 Hz, never "
      "above 1000). Out of range changes nothing, so H 0 is a query.",
      "H: <ms>, <Timer1 counts> (counts = ms x 1000 / 16)",
      "H 80 -> H: 80, 5000" },
    { "F <Hz>", true,
      "Trigger rate of both stations, 1 to 50 Hz (default 10). The transmitter "
      "stores it and reboots when the rate is new to it; the sampling time is set "
      "to its maximum, 80 % of the new period. Out of range changes nothing, so "
      "F 0 is a query.",
      "F: <Hz>, <Timer1 TOP>, <sampling ms>",
      "F 20 -> F: 20, 3124, 40" },
    { "B <baud>", false,
      "Base-station serial baud rate, stored in EEPROM. The reply goes out at the "
      "old rate, then the base-station reboots at the new rate; the ground station "
      "waits the boot time (7 s), then switches the host port to the new rate in "
      "place. The host supports 9600, 19200, 38400, 57600 and 115200; another rate "
      "the base-station would accept is refused and not sent. An invalid rate "
      "changes nothing, so B 0 is a query.",
      "B: <stored baud> (the new rate after a change, the current one for a "
      "query), or ERR: B <baud> unsupported by host (from the ground station)",
      "B 57600 -> B: 57600" },
    { "M", true,
      "Reset every stored setting to its default: 2000 us burst delay, 80 ms "
      "sampling, 10 Hz (sent to the transmitter), 38400 baud. If that changes the "
      "baud rate, the base-station reboots and the ground station follows after "
      "the boot time.",
      "M: OK",
      "M -> M: OK" },
};

//! @brief Display columns of a UTF-8 string of single-width glyphs (counts
//!        lead bytes).
//! @param s UTF-8 string.
//! @return Number of glyphs in s.
int cols(const std::string& s){
    int c = 0;
    for(unsigned char ch : s) if((ch & 0xC0) != 0x80) ++c;
    return c;
}

//! @brief Remove trailing spaces.
//! @param s String to trim in place.
void rtrim(std::string& s){
    while(!s.empty() && s.back() == ' ') s.pop_back();
}

} // namespace

std::vector<std::string> wrapText(const std::string& prefix, const std::string& text,
                                  int indent, int width){
    if(indent > width - 1) indent = width > 1 ? width - 1 : 0;
    std::vector<std::string> out;
    std::istringstream words(text);
    std::string word;
    std::string cur = prefix;
    int curCols = cols(prefix);
    bool hasWord = false;
    auto flush = [&]{
        rtrim(cur);
        out.push_back(cur);
        cur.assign(static_cast<size_t>(indent), ' ');
        curCols = indent;
        hasWord = false;
    };
    while(words >> word){
        int wc = cols(word);
        if(hasWord && curCols + 1 + wc > width) flush();
        //! the prefix leaves no room for the word: start the text on the next line
        if(!hasWord && curCols + wc > width && curCols > indent) flush();
        //! a word longer than a whole line is cut (ASCII text assumed)
        while(!hasWord && curCols + wc > width){
            int fit = std::max(1, width - curCols);
            cur += word.substr(0, static_cast<size_t>(fit));
            word.erase(0, static_cast<size_t>(fit));
            wc = cols(word);
            flush();
        }
        if(wc == 0) continue;
        if(hasWord){ cur += ' '; ++curCols; }
        cur += word;
        curCols += wc;
        hasWord = true;
    }
    //! last line (or the prefix alone, e.g. an entry without text)
    if(hasWord || out.empty() || cur.find_first_not_of(' ') != std::string::npos) flush();
    return out;
}

Writer::Writer(int width)
    : width_(width < 20 ? 20 : width), wide_(width >= WIDE_MIN) {}

void Writer::wrap(Line::Kind kind, const std::string& prefix, int keyCols,
                  const std::string& text, int indent){
    for(const std::string& l : wrapText(prefix, text, indent, width_)){
        lines_.push_back({kind, l, keyCols});
        keyCols = 0;
    }
}

void Writer::title(const std::string& text){
    wrap(Line::Kind::Title, "", 0, text, 0);
}

void Writer::text(const std::string& text){
    wrap(Line::Kind::Text, "", 0, text, 0);
}

void Writer::blank(){
    lines_.push_back({Line::Kind::Text, "", 0});
}

void Writer::entry(const std::string& key, const std::string& text){
    if(wide_ && cols(key) < KEY_COLS){
        std::string prefix = key + std::string(static_cast<size_t>(KEY_COLS - cols(key)), ' ');
        wrap(Line::Kind::Command, prefix, cols(key), text, KEY_COLS);
    } else {
        lines_.push_back({Line::Kind::Command, key, cols(key)});
        wrap(Line::Kind::Text, "  ", 0, text, 2);
    }
}

void Writer::detail(const std::string& label, const std::string& text){
    if(wide_){
        std::string prefix = std::string(KEY_COLS, ' ') + label;
        prefix += std::string(static_cast<size_t>(LABEL_COLS - cols(label)), ' ');
        wrap(Line::Kind::Detail, prefix, KEY_COLS + cols(label), text, KEY_COLS + LABEL_COLS);
    } else {
        wrap(Line::Kind::Detail, "  " + label + " ", 2 + cols(label), text, 4);
    }
}

bool isHelpRequest(const std::string& line){
    const char* blanks = " \t\r\n";
    size_t b = line.find_first_not_of(blanks);
    if(b == std::string::npos) return false;
    size_t e = line.find_last_not_of(blanks);
    std::string w = line.substr(b, e - b + 1);
    for(char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return w == "help" || w == "?";
}

void appendReference(Writer& w){
    w.title("BASE-STATION COMMANDS");
    w.text("Type a command on the TUI command line, or send it as one line to the TCP "
           "command port. Letters are upper case; H, F and B take an integer (H 80 or "
           "H80). A reply starts with its marker letter and a colon. Every command "
           "except C ends continuous mode. [TX] = also acts on the transmitter "
           "station, which the base-station drives over the radio.");
    w.blank();
    for(const Entry& e : BASE_STATION){
        w.entry(std::string(e.syntax) + (e.transmitter ? " [TX]" : ""), e.summary);
        w.detail("Reply", e.reply);
        w.detail("Example", e.example);
    }
    w.blank();
    w.title("GROUND STATION");
    w.entry("help, ?", "Print this reference (any case). The ground station answers it "
                       "itself, also while the base-station boots; nothing is sent to "
                       "the base-station.");
    w.detail("Reply", "these lines (on TCP to the asking client only)");
    w.text("A line that is not a command above gets no reply: TCP drops it, the TUI "
           "sends it to the base-station as typed. While the base-station boots (7 s "
           "after the port opens, or after B or M reboot it) commands are not sent: "
           "TCP drops them, the TUI shows BASE-STATION STILL BOOTING. TCP: one command "
           "per segment; replies go to every connected client. A connection over the "
           "client limit gets E: server full and is closed.");
}

std::string referenceText(int width){
    Writer w(width);
    appendReference(w);
    std::string out;
    for(const Line& l : w.lines()){
        out += l.text;
        out += '\n';
    }
    return out;
}

} // namespace cmdhelp
